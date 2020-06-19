/*
	MIT License

	Copyright (c) 2020 Oleksiy Ryabchun

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

#include "stdafx.h"
#include "OpenDraw.h"
#include "Resource.h"
#include "CommCtrl.h"
#include "Main.h"
#include "Config.h"
#include "Window.h"
#include "ShaderGroup.h"

DWORD __fastcall GetPow2(DWORD value)
{
	DWORD res = 1;
	while (res < value)
		res <<= 1;
	return res;
}

DWORD __stdcall RenderThread(LPVOID lpParameter)
{
	OpenDraw* ddraw = (OpenDraw*)lpParameter;
	do
	{
		if (ddraw->mode && ddraw->attachedSurface && ddraw->attachedSurface->width)
		{
			ddraw->hDc = ::GetDC(ddraw->hDraw);
			{
				if (!::GetPixelFormat(ddraw->hDc))
				{
					PIXELFORMATDESCRIPTOR pfd;
					INT glPixelFormat = GL::PreparePixelFormat(&pfd);
					if (!glPixelFormat)
					{
						glPixelFormat = ::ChoosePixelFormat(ddraw->hDc, &pfd);
						if (!glPixelFormat)
							Main::ShowError(IDS_ERROR_CHOOSE_PF, "OpenDraw.cpp", __LINE__);
						else if (pfd.dwFlags & PFD_NEED_PALETTE)
							Main::ShowError(IDS_ERROR_NEED_PALETTE, "OpenDraw.cpp", __LINE__);
					}

					GL::ResetPixelFormatDescription(&pfd);
					if (::DescribePixelFormat(ddraw->hDc, glPixelFormat, sizeof(PIXELFORMATDESCRIPTOR), &pfd) == NULL)
						Main::ShowError(IDS_ERROR_DESCRIBE_PF, "OpenDraw.cpp", __LINE__);

					if (!::SetPixelFormat(ddraw->hDc, glPixelFormat, &pfd))
						Main::ShowError(IDS_ERROR_SET_PF, "OpenDraw.cpp", __LINE__);

					if ((pfd.iPixelType != PFD_TYPE_RGBA) || (pfd.cRedBits < 5) || (pfd.cGreenBits < 6) || (pfd.cBlueBits < 5))
						Main::ShowError(IDS_ERROR_BAD_PF, "OpenDraw.cpp", __LINE__);
				}

				HGLRC hRc = wglCreateContext(ddraw->hDc);
				if (hRc)
				{
					if (wglMakeCurrent(ddraw->hDc, hRc))
					{
						GL::CreateContextAttribs(ddraw->hDc, &hRc);
						if (config.gl.version.value >= GL_VER_2_0)
						{
							DWORD glMaxTexSize;
							GLGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint*)&glMaxTexSize);
							if (glMaxTexSize < GetPow2(ddraw->mode->width > ddraw->mode->height ? ddraw->mode->width : ddraw->mode->height))
								config.gl.version.value = GL_VER_1_1;
						}

						config.gl.version.real = config.gl.version.value;
						switch (config.renderer)
						{
						case RendererOpenGL1:
							if (config.gl.version.value > GL_VER_1_1)
								config.gl.version.value = GL_VER_1_2;
							break;

						case RendererOpenGL2:
							if (config.gl.version.value >= GL_VER_2_0)
								config.gl.version.value = GL_VER_2_0;
							else
								config.renderer = RendererAuto;
							break;

						case RendererOpenGL3:
							if (config.gl.version.value >= GL_VER_3_0)
								config.gl.version.value = GL_VER_3_0;
							else
								config.renderer = RendererAuto;
							break;

						default:
							break;
						}

						if (config.gl.version.value >= GL_VER_3_0)
							ddraw->RenderNew();
						else if (config.gl.version.value >= GL_VER_2_0)
							ddraw->RenderMid();
						else
							ddraw->RenderOld();

						wglMakeCurrent(ddraw->hDc, NULL);
					}

					wglDeleteContext(hRc);
				}
			}
			::ReleaseDC(ddraw->hDraw, ddraw->hDc);
			ddraw->hDc = NULL;
			break;
		}

		Sleep(0);
	} while (!ddraw->isFinish);

	return NULL;
}

VOID OpenDraw::RenderOld()
{
	if (this->filterState.interpolation > InterpolateLinear)
		this->filterState.interpolation = InterpolateLinear;

	PostMessage(this->hWnd, config.msgMenu, NULL, NULL);

	DWORD glMaxTexSize;
	GLGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint*)&glMaxTexSize);
	if (glMaxTexSize < 256)
		glMaxTexSize = 256;

	DWORD maxAllow = GetPow2(this->mode->width > this->mode->height ? this->mode->width : this->mode->height);
	DWORD maxTexSize = maxAllow < glMaxTexSize ? maxAllow : glMaxTexSize;

	DWORD framePerWidth = this->mode->width / maxTexSize + (this->mode->width % maxTexSize ? 1 : 0);
	DWORD framePerHeight = this->mode->height / maxTexSize + (this->mode->height % maxTexSize ? 1 : 0);
	DWORD frameCount = framePerWidth * framePerHeight;
	Frame* frames = (Frame*)MemoryAlloc(frameCount * sizeof(Frame));
	{
		Frame* frame = frames;
		for (DWORD y = 0; y < this->mode->height; y += maxTexSize)
		{
			DWORD height = this->mode->height - y;
			if (height > maxTexSize)
				height = maxTexSize;

			for (DWORD x = 0; x < this->mode->width; x += maxTexSize, ++frame)
			{
				DWORD width = this->mode->width - x;
				if (width > maxTexSize)
					width = maxTexSize;

				frame->rect.x = x;
				frame->rect.y = y;
				frame->rect.width = width;
				frame->rect.height = height;

				frame->vSize.width = x + width;
				frame->vSize.height = y + height;

				frame->tSize.width = width == maxTexSize ? 1.0f : (FLOAT)width / maxTexSize;
				frame->tSize.height = height == maxTexSize ? 1.0f : (FLOAT)height / maxTexSize;

				GLGenTextures(1, &frame->id);
				GLBindTexture(GL_TEXTURE_2D, frame->id);

				GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, config.gl.caps.clampToEdge);
				GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, config.gl.caps.clampToEdge);
				GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
				GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
				GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

				GLTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

				if (config.gl.version.value > GL_VER_1_1)
					GLTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, maxTexSize, maxTexSize, GL_NONE, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
				else
					GLTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, maxTexSize, maxTexSize, GL_NONE, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			}
		}

		GLMatrixMode(GL_PROJECTION);
		GLLoadIdentity();
		GLOrtho(0.0, (GLdouble)this->mode->width, (GLdouble)this->mode->height, 0.0, 0.0, 1.0);
		GLMatrixMode(GL_MODELVIEW);
		GLLoadIdentity();

		GLEnable(GL_TEXTURE_2D);
		GLClearColor(0.0f, 0.0f, 0.0f, 1.0f);

		VOID* frameBuffer = NULL;
		BOOL isPixelStore = config.gl.version.value > GL_VER_1_1;
		if (!isPixelStore)
			frameBuffer = MemoryAlloc(maxTexSize * maxTexSize * (config.gl.version.value > GL_VER_1_1 ? sizeof(WORD) : sizeof(DWORD)));
		{
			BOOL isVSync = config.image.vSync;
			if (WGLSwapInterval)
				WGLSwapInterval(isVSync);

			BOOL first = TRUE;
			DWORD clear = 0;
			do
			{
				OpenDrawSurface* surface = this->attachedSurface;
				if (!surface)
					continue;

				if (isVSync != config.image.vSync)
				{
					isVSync = config.image.vSync;
					if (WGLSwapInterval)
						WGLSwapInterval(isVSync);
				}

				UpdateRect* updateClip = surface->poinetrClip;
				UpdateRect* finClip = surface->currentClip;
				surface->poinetrClip = finClip;

				if (this->CheckView())
				{
					GLViewport(this->viewport.rectangle.x, this->viewport.rectangle.y, this->viewport.rectangle.width, this->viewport.rectangle.height);
					clear = 0;
				}

				if (clear++ <= 1)
					GLClear(GL_COLOR_BUFFER_BIT);

				DWORD glFilter = 0;
				FilterState state = this->filterState;
				this->filterState.flags = FALSE;
				if (state.flags)
					glFilter = state.interpolation == InterpolateNearest ? GL_NEAREST : GL_LINEAR;

				if (first)
				{
					first = FALSE;

					updateClip = (finClip == surface->clipsList ? surface->endClip : finClip) - 1;
					updateClip->rect.left = 0;
					updateClip->rect.top = 0;
					updateClip->rect.right = this->mode->width;
					updateClip->rect.bottom = this->mode->height;
					updateClip->isActive = TRUE;
				}

				if (isPixelStore)
					GLPixelStorei(GL_UNPACK_ROW_LENGTH, this->mode->width);
				{
					DWORD count = frameCount;
					frame = frames;
					while (count--)
					{
						if (frameCount == 1)
						{
							if (glFilter)
							{
								GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
								GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
							}

							while (updateClip != finClip)
							{
								if (updateClip->isActive)
								{
									RECT update = updateClip->rect;
									DWORD texWidth = update.right - update.left;
									DWORD texHeight = update.bottom - update.top;

									if (texWidth == this->mode->width)
									{
										if (config.gl.version.value > GL_VER_1_1)
											GLTexSubImage2D(GL_TEXTURE_2D, 0, 0, update.top, texWidth, texHeight, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, surface->indexBuffer + update.top * texWidth);
										else
										{
											WORD* source = surface->indexBuffer + update.top * texWidth;
											DWORD* dest = (DWORD*)frameBuffer;
											DWORD copyWidth = texWidth;
											DWORD copyHeight = texHeight;
											do
											{
												WORD* src = source;
												source += this->mode->width;

												DWORD count = copyWidth;
												do
												{
													WORD px = *src++;
													*dest++ = ((px & 0xF800) >> 8) | ((px & 0x07E0) << 5) | ((px & 0x001F) << 19);
												} while (--count);
											} while (--copyHeight);

											GLTexSubImage2D(GL_TEXTURE_2D, 0, 0, update.top, texWidth, texHeight, GL_RGBA, GL_UNSIGNED_BYTE, frameBuffer);
										}
									}
									else
									{
										if (texWidth & 1)
										{
											++texWidth;
											if (update.left)
												--update.left;
											else
												++update.right;
										}

										if (config.gl.version.value > GL_VER_1_1)
											GLTexSubImage2D(GL_TEXTURE_2D, 0, update.left, update.top, texWidth, texHeight, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, surface->indexBuffer + update.top * this->mode->width + update.left);
										else
										{
											WORD* source = surface->indexBuffer + update.top * this->mode->width + update.left;
											DWORD* dest = (DWORD*)frameBuffer;
											DWORD copyWidth = texWidth;
											DWORD copyHeight = texHeight;
											do
											{
												WORD* src = source;
												source += this->mode->width;

												DWORD count = copyWidth;
												do
												{
													WORD px = *src++;
													*dest++ = ((px & 0xF800) >> 8) | ((px & 0x07E0) << 5) | ((px & 0x001F) << 19);
												} while (--count);
											} while (--copyHeight);

											GLTexSubImage2D(GL_TEXTURE_2D, 0, update.left, update.top, texWidth, texHeight, GL_RGBA, GL_UNSIGNED_BYTE, frameBuffer);
										}
									}
								}

								if (++updateClip == surface->endClip)
									updateClip = surface->clipsList;
							}
						}
						else
						{
							GLBindTexture(GL_TEXTURE_2D, frame->id);

							if (glFilter)
							{
								GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
								GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
							}

							Rect* rect = &frame->rect;
							INT rect_right = rect->x + rect->width;
							INT rect_bottom = rect->y + rect->height;

							UpdateRect* update = updateClip;
							while (update != finClip)
							{
								if (update->isActive)
								{
									RECT clip = {
										rect->x > update->rect.left ? rect->x : update->rect.left,
										rect->y > update->rect.top ? rect->y : update->rect.top,
										rect_right < update->rect.right ? rect_right : update->rect.right,
										rect_bottom < update->rect.bottom ? rect_bottom : update->rect.bottom
									};

									INT clipWidth = clip.right - clip.left;
									INT clipHeight = clip.bottom - clip.top;
									if (clipWidth > 0 && clipHeight > 0)
									{
										if (clipWidth & 1)
										{
											++clipWidth;
											if (clip.left != rect->x)
												--clip.left;
											else
												++clip.right;
										}

										if (config.gl.version.value > GL_VER_1_1)
											GLTexSubImage2D(GL_TEXTURE_2D, 0, clip.left - rect->x, clip.top - rect->y, clipWidth, clipHeight, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, surface->indexBuffer + clip.top * this->mode->width + clip.left);
										else
										{
											WORD* source = surface->indexBuffer + clip.top * this->mode->width + clip.left;
											DWORD* dest = (DWORD*)frameBuffer;
											DWORD copyWidth = clipWidth;
											DWORD copyHeight = clipHeight;
											do
											{
												WORD* src = source;
												source += this->mode->width;

												DWORD count = copyWidth;
												do
												{
													WORD px = *src++;
													*dest++ = ((px & 0xF800) >> 8) | ((px & 0x07E0) << 5) | ((px & 0x001F) << 19);
												} while (--count);
											} while (--copyHeight);

											GLTexSubImage2D(GL_TEXTURE_2D, 0, clip.left - rect->x, clip.top - rect->y, clipWidth, clipHeight, GL_RGBA, GL_UNSIGNED_BYTE, frameBuffer);
										}
									}
								}

								if (++update == surface->endClip)
									update = surface->clipsList;
							}
						}

						GLBegin(GL_TRIANGLE_FAN);
						{
							GLTexCoord2f(0.0f, 0.0f);
							GLVertex2s(frame->rect.x, frame->rect.y);

							GLTexCoord2f(frame->tSize.width, 0.0f);
							GLVertex2s(frame->vSize.width, frame->rect.y);

							GLTexCoord2f(frame->tSize.width, frame->tSize.height);
							GLVertex2s(frame->vSize.width, frame->vSize.height);

							GLTexCoord2f(0.0f, frame->tSize.height);
							GLVertex2s(frame->rect.x, frame->vSize.height);
						}
						GLEnd();
						++frame;
					}
				}
				if (isPixelStore)
					GLPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

				if (this->isTakeSnapshot)
				{
					this->isTakeSnapshot = FALSE;
					surface->TakeSnapshot();
				}

				SwapBuffers(this->hDc);
				GLFinish();

				if (clear >= 2)
					WaitForSingleObject(this->hDrawEvent, INFINITE);
			} while (!this->isFinish);
		}
		if (!isPixelStore)
			MemoryFree(frameBuffer);

		frame = frames;
		DWORD count = frameCount;
		while (count--)
		{
			GLDeleteTextures(1, &frame->id);
			++frame;
		}
	}
	MemoryFree(frames);
}

VOID OpenDraw::RenderMid()
{
	PostMessage(this->hWnd, config.msgMenu, NULL, NULL);

	DWORD maxTexSize = GetPow2(this->mode->width > this->mode->height ? this->mode->width : this->mode->height);
	FLOAT texWidth = this->mode->width == maxTexSize ? 1.0f : (FLOAT)this->mode->width / maxTexSize;
	FLOAT texHeight = this->mode->height == maxTexSize ? 1.0f : (FLOAT)this->mode->height / maxTexSize;

	DWORD texSize = (maxTexSize & 0xFFFF) | (maxTexSize << 16);

	struct {
		ShaderGroup* linear;
		ShaderGroup* hermite;
		ShaderGroup* cubic;
	} shaders = {
		new ShaderGroup(GLSL_VER_1_10, IDR_LINEAR_VERTEX, IDR_LINEAR_FRAGMENT, SHADER_LEVELS, NULL),
		new ShaderGroup(GLSL_VER_1_10, IDR_HERMITE_VERTEX, IDR_HERMITE_FRAGMENT, SHADER_LEVELS, NULL),
		new ShaderGroup(GLSL_VER_1_10, IDR_CUBIC_VERTEX, IDR_CUBIC_FRAGMENT, SHADER_LEVELS, NULL)
	};

	ShaderGroup* program = NULL;
	{
		GLuint bufferName;
		GLGenBuffers(1, &bufferName);
		{
			GLBindBuffer(GL_ARRAY_BUFFER, bufferName);
			{
				{
					FLOAT buffer[4][8] = {
						{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
						{ (FLOAT)this->mode->width, 0.0f, 0.0f, 1.0f, texWidth, 0.0f, 0.0f, 0.0f },
						{ (FLOAT)this->mode->width, (FLOAT)this->mode->height, 0.0f, 1.0f, texWidth, texHeight, 0.0f, 0.0f },
						{ 0.0f, (FLOAT)this->mode->height, 0.0f, 1.0f, 0.0f, texHeight, 0.0f, 0.0f }
					};

					FLOAT mvp[4][4] = {
						{ FLOAT(2.0f / this->mode->width), 0.0f, 0.0f, 0.0f },
						{ 0.0f, FLOAT(-2.0f / this->mode->height), 0.0f, 0.0f },
						{ 0.0f, 0.0f, 2.0f, 0.0f },
						{ -1.0f, 1.0f, -1.0f, 1.0f }
					};

					for (DWORD i = 0; i < 4; ++i)
					{
						FLOAT* vector = &buffer[i][0];
						for (DWORD j = 0; j < 4; ++j)
						{
							FLOAT sum = 0.0f;
							for (DWORD v = 0; v < 4; ++v)
								sum += mvp[v][j] * vector[v];

							vector[j] = sum;
						}
					}

					GLBufferData(GL_ARRAY_BUFFER, sizeof(buffer), buffer, GL_STATIC_DRAW);
				}

				{
					GLEnableVertexAttribArray(0);
					GLVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (GLvoid*)0);

					GLEnableVertexAttribArray(1);
					GLVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (GLvoid*)16);
				}

				GLuint textureId;
				GLGenTextures(1, &textureId);
				{
					GLActiveTexture(GL_TEXTURE0);
					GLBindTexture(GL_TEXTURE_2D, textureId);
					GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, config.gl.caps.clampToEdge);
					GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, config.gl.caps.clampToEdge);
					GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
					GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
					GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					GLTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, maxTexSize, maxTexSize, GL_NONE, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);

					GLClearColor(0.0f, 0.0f, 0.0f, 1.0f);

					BOOL isVSync = config.image.vSync;
					if (WGLSwapInterval)
						WGLSwapInterval(isVSync);

					BOOL first = TRUE;
					DWORD clear = 0;
					do
					{
						OpenDrawSurface* surface = this->attachedSurface;
						if (!surface)
							continue;

						if (isVSync != config.image.vSync)
						{
							isVSync = config.image.vSync;
							if (WGLSwapInterval)
								WGLSwapInterval(isVSync);
						}

						FilterState state = this->filterState;
						this->filterState.flags = FALSE;

						if (program && program->Check())
							state.flags = TRUE;

						if (state.flags)
							this->viewport.refresh = TRUE;

						BOOL isTakeSnapshot = this->isTakeSnapshot;
						if (isTakeSnapshot)
							this->isTakeSnapshot = FALSE;

						UpdateRect* updateClip = surface->poinetrClip;
						UpdateRect* finClip = surface->currentClip;
						surface->poinetrClip = finClip;

						if (this->CheckView())
						{
							GLViewport(this->viewport.rectangle.x, this->viewport.rectangle.y, this->viewport.rectangle.width, this->viewport.rectangle.height);
							clear = 0;
						}

						if (clear++ <= 1)
							GLClear(GL_COLOR_BUFFER_BIT);

						if (state.flags)
						{
							switch (state.interpolation)
							{
							case InterpolateHermite:
								program = shaders.hermite;
								break;
							case InterpolateCubic:
								program = shaders.cubic;
								break;
							default:
								program = shaders.linear;
								break;
							}

							program->Use(texSize);

							DWORD filter = state.interpolation == InterpolateLinear || state.interpolation == InterpolateHermite ? GL_LINEAR : GL_NEAREST;
							GLBindTexture(GL_TEXTURE_2D, textureId);
							GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
							GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
						}

						if (first)
						{
							first = FALSE;

							updateClip = (finClip == surface->clipsList ? surface->endClip : finClip) - 1;
							updateClip->rect.left = 0;
							updateClip->rect.top = 0;
							updateClip->rect.right = this->mode->width;
							updateClip->rect.bottom = this->mode->height;
							updateClip->isActive = TRUE;
						}

						// NEXT UNCHANGED
						{
							// Update texture
							GLPixelStorei(GL_UNPACK_ROW_LENGTH, this->mode->width);
							while (updateClip != finClip)
							{
								if (updateClip->isActive)
								{
									RECT update = updateClip->rect;
									DWORD texWidth = update.right - update.left;
									DWORD texHeight = update.bottom - update.top;

									if (texWidth == this->mode->width)
										GLTexSubImage2D(GL_TEXTURE_2D, 0, 0, update.top, texWidth, texHeight, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, surface->indexBuffer + update.top * texWidth);
									else
									{
										if (texWidth & 1)
										{
											++texWidth;
											if (update.left)
												--update.left;
											else
												++update.right;
										}

										GLTexSubImage2D(GL_TEXTURE_2D, 0, update.left, update.top, texWidth, texHeight, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, surface->indexBuffer + update.top * this->mode->width + update.left);
									}
								}

								if (++updateClip == surface->endClip)
									updateClip = surface->clipsList;
							}
							GLPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

							GLDrawArrays(GL_TRIANGLE_FAN, 0, 4);
						}

						if (isTakeSnapshot)
							surface->TakeSnapshot();

						SwapBuffers(this->hDc);
						GLFinish();

						if (clear >= 2)
							WaitForSingleObject(this->hDrawEvent, INFINITE);
					} while (!this->isFinish);
				}
				GLDeleteTextures(1, &textureId);
			}
			GLBindBuffer(GL_ARRAY_BUFFER, NULL);
		}
		GLDeleteBuffers(1, &bufferName);
	}
	GLUseProgram(NULL);

	ShaderGroup** shader = (ShaderGroup**)&shaders;
	DWORD count = sizeof(shaders) / sizeof(ShaderGroup*);
	do
		delete *shader++;
	while (--count);
}

VOID OpenDraw::RenderNew()
{
	PostMessage(this->hWnd, config.msgMenu, NULL, NULL);

	DWORD maxTexSize = GetPow2(this->mode->width > this->mode->height ? this->mode->width : this->mode->height);
	FLOAT texWidth = this->mode->width == maxTexSize ? 1.0f : (FLOAT)this->mode->width / maxTexSize;
	FLOAT texHeight = this->mode->height == maxTexSize ? 1.0f : (FLOAT)this->mode->height / maxTexSize;

	DWORD texSize = (maxTexSize & 0xFFFF) | (maxTexSize << 16);

	FLOAT mvp[4][4] = {
		{ FLOAT(2.0f / this->mode->width), 0.0f, 0.0f, 0.0f },
		{ 0.0f, FLOAT(-2.0f / this->mode->height), 0.0f, 0.0f },
		{ 0.0f, 0.0f, 2.0f, 0.0f },
		{ -1.0f, 1.0f, -1.0f, 1.0f }
	};

	struct {
		ShaderGroup* stencil;
		ShaderGroup* linear;
		ShaderGroup* hermite;
		ShaderGroup* cubic;
		ShaderGroup* xBRz_2x;
		ShaderGroup* xBRz_3x;
		ShaderGroup* xBRz_4x;
		ShaderGroup* xBRz_5x;
		ShaderGroup* xBRz_6x;
		ShaderGroup* scaleHQ_2x;
		ShaderGroup* scaleHQ_4x;
		ShaderGroup* xSal_2x;
		ShaderGroup* eagle_2x;
		ShaderGroup* scaleNx_2x;
		ShaderGroup* scaleNx_3x;
	} shaders = {
		new ShaderGroup(GLSL_VER_1_30, IDR_STENCIL_VERTEX, IDR_STENCIL_FRAGMENT, NULL, (GLfloat*)mvp),
		new ShaderGroup(GLSL_VER_1_30, IDR_LINEAR_VERTEX, IDR_LINEAR_FRAGMENT, SHADER_LEVELS, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_HERMITE_VERTEX, IDR_HERMITE_FRAGMENT, SHADER_LEVELS, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_CUBIC_VERTEX, IDR_CUBIC_FRAGMENT, SHADER_LEVELS, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_XBRZ_VERTEX, IDR_XBRZ_FRAGMENT_2X, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_XBRZ_VERTEX, IDR_XBRZ_FRAGMENT_3X, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_XBRZ_VERTEX, IDR_XBRZ_FRAGMENT_4X, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_XBRZ_VERTEX, IDR_XBRZ_FRAGMENT_5X, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_XBRZ_VERTEX, IDR_XBRZ_FRAGMENT_6X, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_SCALEHQ_VERTEX_2X, IDR_SCALEHQ_FRAGMENT_2X, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_SCALEHQ_VERTEX_4X, IDR_SCALEHQ_FRAGMENT_4X, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_XSAL_VERTEX, IDR_XSAL_FRAGMENT, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_EAGLE_VERTEX, IDR_EAGLE_FRAGMENT, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_SCALENX_VERTEX_2X, IDR_SCALENX_FRAGMENT_2X, NULL, NULL),
		new ShaderGroup(GLSL_VER_1_30, IDR_SCALENX_VERTEX_3X, IDR_SCALENX_FRAGMENT_3X, NULL, NULL)
	};

	ShaderGroup* program = NULL;
	ShaderGroup* upscaleProgram = NULL;
	{
		POINTFLOAT* stencil = NULL;
		GLuint stArrayName, stBufferName, arrayName;

		GLGenVertexArrays(1, &arrayName);
		{
			GLBindVertexArray(arrayName);
			{
				GLuint bufferName;
				GLGenBuffers(1, &bufferName);
				{
					GLBindBuffer(GL_ARRAY_BUFFER, bufferName);
					{
						{
							FLOAT buffer[8][8] = {
								{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
								{ (FLOAT)this->mode->width, 0.0f, 0.0f, 1.0f, texWidth, 0.0f, 0.0f, 0.0f },
								{ (FLOAT)this->mode->width, (FLOAT)this->mode->height, 0.0f, 1.0f, texWidth, texHeight, 0.0f, 0.0f },
								{ 0.0f, (FLOAT)this->mode->height, 0.0f, 1.0f, 0.0f, texHeight, 0.0f, 0.0f },

								{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f },
								{ (FLOAT)this->mode->width, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f },
								{ (FLOAT)this->mode->width, (FLOAT)this->mode->height, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f },
								{ 0.0f, (FLOAT)this->mode->height, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }
							};

							for (DWORD i = 0; i < 8; ++i)
							{
								FLOAT* vector = &buffer[i][0];
								for (DWORD j = 0; j < 4; ++j)
								{
									FLOAT sum = 0.0f;
									for (DWORD v = 0; v < 4; ++v)
										sum += mvp[v][j] * vector[v];

									vector[j] = sum;
								}
							}

							GLBufferData(GL_ARRAY_BUFFER, sizeof(buffer), buffer, GL_STATIC_DRAW);
						}

						{
							GLEnableVertexAttribArray(0);
							GLVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (GLvoid*)0);

							GLEnableVertexAttribArray(1);
							GLVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (GLvoid*)16);
						}

						GLuint textureId;
						GLGenTextures(1, &textureId);
						{
							GLActiveTexture(GL_TEXTURE0);
							GLBindTexture(GL_TEXTURE_2D, textureId);
							GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, config.gl.caps.clampToEdge);
							GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, config.gl.caps.clampToEdge);
							GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
							GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
							GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
							GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
							GLTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, maxTexSize, maxTexSize, GL_NONE, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);

							GLuint fboId;
							GLGenFramebuffers(1, &fboId);
							{
								DWORD viewSize = 0;
								GLuint rboId = 0, tboId = 0;
								{
									GLClearColor(0.0f, 0.0f, 0.0f, 1.0f);

									BOOL isVSync = config.image.vSync;
									if (WGLSwapInterval)
										WGLSwapInterval(isVSync);

									BOOL first = TRUE;
									DWORD clear = 0;
									do
									{
										OpenDrawSurface* surface = this->attachedSurface;
										if (!surface)
											continue;

										if (isVSync != config.image.vSync)
										{
											isVSync = config.image.vSync;
											if (WGLSwapInterval)
												WGLSwapInterval(isVSync);
										}

										FilterState state = this->filterState;
										this->filterState.flags = FALSE;

										if (program && program->Check())
											state.flags = TRUE;

										if (state.flags)
											this->viewport.refresh = TRUE;

										BOOL isTakeSnapshot = this->isTakeSnapshot;
										if (isTakeSnapshot)
											this->isTakeSnapshot = FALSE;

										UpdateRect* updateClip = surface->poinetrClip;
										UpdateRect* finClip = surface->currentClip;
										surface->poinetrClip = finClip;

										if (state.upscaling)
										{
											GLBindFramebuffer(GL_DRAW_FRAMEBUFFER, fboId);

											if (state.flags)
											{
												switch (state.upscaling)
												{
												case UpscaleScaleNx:
													switch (state.value)
													{
													case 3:
														upscaleProgram = shaders.scaleNx_3x;
														break;
													default:
														upscaleProgram = shaders.scaleNx_2x;
														break;
													}

													break;

												case UpscaleScaleHQ:
													switch (state.value)
													{
													case 4:
														upscaleProgram = shaders.scaleHQ_4x;
														break;
													default:
														upscaleProgram = shaders.scaleHQ_2x;
														break;
													}

													break;

												case UpscaleXRBZ:
													switch (state.value)
													{
													case 6:
														upscaleProgram = shaders.xBRz_6x;
														break;
													case 5:
														upscaleProgram = shaders.xBRz_5x;
														break;
													case 4:
														upscaleProgram = shaders.xBRz_4x;
														break;
													case 3:
														upscaleProgram = shaders.xBRz_3x;
														break;
													default:
														upscaleProgram = shaders.xBRz_2x;
														break;
													}

													break;

												case UpscaleXSal:
													upscaleProgram = shaders.xSal_2x;

													break;

												default:
													upscaleProgram = shaders.eagle_2x;

													break;
												}

												DWORD newSize = MAKELONG(this->mode->width * state.value, this->mode->height * state.value);
												if (newSize != viewSize)
												{
													first = TRUE;

													if (!viewSize)
													{
														GLGenTextures(1, &tboId);
														GLGenRenderbuffers(1, &rboId);
													}

													viewSize = newSize;

													// Gen texture
													GLBindTexture(GL_TEXTURE_2D, tboId);
													GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, config.gl.caps.clampToEdge);
													GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, config.gl.caps.clampToEdge);
													GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
													GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
													GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
													GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
													GLTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LOWORD(viewSize), HIWORD(viewSize), GL_NONE, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

													// Get storage
													GLBindRenderbuffer(GL_RENDERBUFFER, rboId);
													GLRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, LOWORD(viewSize), HIWORD(viewSize));
													GLBindRenderbuffer(GL_RENDERBUFFER, NULL);

													GLFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tboId, 0);
													GLFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rboId);

													if (!stencil)
													{
														DWORD size = STENCIL_COUNT * sizeof(POINTFLOAT) * STENCIL_POINTS;
														stencil = (POINTFLOAT*)MemoryAlloc(size);

														{
															GLGenVertexArrays(1, &stArrayName);
															GLBindVertexArray(stArrayName);
															GLGenBuffers(1, &stBufferName);
															GLBindBuffer(GL_ARRAY_BUFFER, stBufferName);
															{
																GLBufferData(GL_ARRAY_BUFFER, size, NULL, GL_STREAM_DRAW);

																GLEnableVertexAttribArray(0);
																GLVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)0);
															}

															GLBindVertexArray(arrayName);
															GLBindBuffer(GL_ARRAY_BUFFER, bufferName);
														}
													}
												}
											}

											GLViewport(0, 0, LOWORD(viewSize), HIWORD(viewSize));

											// Clear and stencil
											if (first)
											{
												first = FALSE;

												updateClip = (finClip == surface->clipsList ? surface->endClip : finClip) - 1;
												updateClip->rect.left = 0;
												updateClip->rect.top = 0;
												updateClip->rect.right = this->mode->width;
												updateClip->rect.bottom = this->mode->height;
												updateClip->isActive = TRUE;
											}
											else
											{
												GLEnable(GL_STENCIL_TEST);
												GLClear(GL_STENCIL_BUFFER_BIT);

												shaders.stencil->Use(0);
												{
													GLColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
													GLStencilFunc(GL_ALWAYS, 0x01, 0x01);
													GLStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
													{
														GLBindVertexArray(stArrayName);
														GLBindBuffer(GL_ARRAY_BUFFER, stBufferName);
														{
															POINTFLOAT* point = stencil;
															UpdateRect* clip = updateClip;
															while (clip != finClip)
															{
																if (clip->isActive)
																{
																	point->x = (FLOAT)clip->rect.left;
																	point->y = (FLOAT)clip->rect.top;
																	++point;
																	point->x = (FLOAT)clip->rect.right;
																	point->y = (FLOAT)clip->rect.top;
																	++point;
																	point->x = (FLOAT)clip->rect.right;
																	point->y = (FLOAT)clip->rect.bottom;
																	++point;

																	point->x = (FLOAT)clip->rect.left;
																	point->y = (FLOAT)clip->rect.top;
																	++point;
																	point->x = (FLOAT)clip->rect.right;
																	point->y = (FLOAT)clip->rect.bottom;
																	++point;
																	point->x = (FLOAT)clip->rect.left;
																	point->y = (FLOAT)clip->rect.bottom;
																	++point;
																}

																if (++clip == surface->endClip)
																	clip = surface->clipsList;
															}

															DWORD count = point - stencil;
															if (count)
															{
																GLBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(POINTFLOAT), stencil);
																GLDrawArrays(GL_TRIANGLES, 0, count);
															}
														}
														GLBindVertexArray(arrayName);
														GLBindBuffer(GL_ARRAY_BUFFER, bufferName);
													}
													GLColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
													GLStencilFunc(GL_EQUAL, 0x01, 0x01);
													GLStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
												}
											}

											upscaleProgram->Use(texSize);

											GLBindTexture(GL_TEXTURE_2D, textureId);
											GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
											GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
										}
										else
										{
											if (this->CheckView())
											{
												GLViewport(this->viewport.rectangle.x, this->viewport.rectangle.y, this->viewport.rectangle.width, this->viewport.rectangle.height);
												clear = 0;
											}

											if (clear++ <= 1)
												GLClear(GL_COLOR_BUFFER_BIT);

											if (state.flags)
											{
												if (viewSize)
												{
													GLDeleteTextures(1, &tboId);
													GLDeleteRenderbuffers(1, &rboId);
													viewSize = 0;
												}

												switch (state.interpolation)
												{
												case InterpolateHermite:
													program = shaders.hermite;
													break;
												case InterpolateCubic:
													program = shaders.cubic;
													break;
												default:
													program = shaders.linear;
													break;
												}

												program->Use(texSize);

												GLBindTexture(GL_TEXTURE_2D, textureId);

												DWORD filter = state.interpolation == InterpolateLinear || state.interpolation == InterpolateHermite ? GL_LINEAR : GL_NEAREST;
												GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
												GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
											}

											if (first)
											{
												first = FALSE;

												updateClip = (finClip == surface->clipsList ? surface->endClip : finClip) - 1;
												updateClip->rect.left = 0;
												updateClip->rect.top = 0;
												updateClip->rect.right = this->mode->width;
												updateClip->rect.bottom = this->mode->height;
												updateClip->isActive = TRUE;
											}
										}

										// NEXT UNCHANGED
										{
											// Update texture
											GLPixelStorei(GL_UNPACK_ROW_LENGTH, this->mode->width);
											while (updateClip != finClip)
											{
												if (updateClip->isActive)
												{
													RECT update = updateClip->rect;
													DWORD texWidth = update.right - update.left;
													DWORD texHeight = update.bottom - update.top;

													if (texWidth == this->mode->width)
														GLTexSubImage2D(GL_TEXTURE_2D, 0, 0, update.top, texWidth, texHeight, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, surface->indexBuffer + update.top * texWidth);
													else
													{
														if (texWidth & 1)
														{
															++texWidth;
															if (update.left)
																--update.left;
															else
																++update.right;
														}

														GLTexSubImage2D(GL_TEXTURE_2D, 0, update.left, update.top, texWidth, texHeight, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, surface->indexBuffer + update.top * this->mode->width + update.left);
													}
												}

												if (++updateClip == surface->endClip)
													updateClip = surface->clipsList;
											}
											GLPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

											// Draw into FBO texture
											GLDrawArrays(GL_TRIANGLE_FAN, 0, 4);
										}

										// Draw from FBO
										if (state.upscaling)
										{
											GLDisable(GL_STENCIL_TEST);
											//GLFinish();
											GLBindFramebuffer(GL_DRAW_FRAMEBUFFER, NULL);

											switch (state.interpolation)
											{
											case InterpolateHermite:
												program = shaders.hermite;
												break;
											case InterpolateCubic:
												program = shaders.cubic;
												break;
											default:
												program = shaders.linear;
												break;
											}

											program->Use(texSize);
											{
												if (this->CheckView())
													clear = 0;

												GLViewport(this->viewport.rectangle.x, this->viewport.rectangle.y, this->viewport.rectangle.width, this->viewport.rectangle.height);

												if (clear++ <= 1)
													GLClear(GL_COLOR_BUFFER_BIT);

												GLBindTexture(GL_TEXTURE_2D, tboId);

												DWORD filter = state.interpolation == InterpolateLinear || state.interpolation == InterpolateHermite ? GL_LINEAR : GL_NEAREST;
												GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
												GLTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);

												GLDrawArrays(GL_TRIANGLE_FAN, 4, 4);

												if (isTakeSnapshot && OpenClipboard(NULL))
												{
													EmptyClipboard();

													DWORD size = LOWORD(viewSize) * HIWORD(viewSize) * 3;
													DWORD slice = sizeof(BITMAPINFOHEADER);
													HGLOBAL hMemory = GlobalAlloc(GMEM_MOVEABLE, slice + size);
													if (hMemory)
													{
														VOID* data = GlobalLock(hMemory);
														if (data)
														{
															BITMAPINFOHEADER* bmi = (BITMAPINFOHEADER*)data;
															bmi->biSize = sizeof(BITMAPINFOHEADER);
															bmi->biWidth = LOWORD(viewSize);
															bmi->biHeight = HIWORD(viewSize);
															bmi->biPlanes = 1;
															bmi->biBitCount = 24;
															bmi->biCompression = BI_RGB;
															bmi->biSizeImage = size;
															bmi->biXPelsPerMeter = 1;
															bmi->biYPelsPerMeter = 1;
															bmi->biClrUsed = 0;
															bmi->biClrImportant = 0;

															GLGetTexImage(GL_TEXTURE_2D, 0, GL_BGR_EXT, GL_UNSIGNED_BYTE, (BYTE*)data + slice);

															GlobalUnlock(hMemory);
															SetClipboardData(CF_DIB, hMemory);
														}

														GlobalFree(hMemory);
													}

													CloseClipboard();
												}
											}
										}
										else if (isTakeSnapshot)
											surface->TakeSnapshot();

										SwapBuffers(this->hDc);
										GLFinish();

										if (clear >= 2)
											WaitForSingleObject(this->hDrawEvent, INFINITE);
									} while (!this->isFinish);
								}

								if (viewSize)
								{
									GLDeleteRenderbuffers(1, &rboId);
									GLDeleteTextures(1, &tboId);
								}
							}
							GLDeleteFramebuffers(1, &fboId);
						}
						GLDeleteTextures(1, &textureId);
					}
					GLBindBuffer(GL_ARRAY_BUFFER, NULL);
				}
				GLDeleteBuffers(1, &bufferName);
			}
			GLBindVertexArray(NULL);
		}
		GLDeleteVertexArrays(1, &arrayName);

		if (stencil)
		{
			MemoryFree(stencil);
			GLDeleteBuffers(1, &stBufferName);
			GLDeleteVertexArrays(1, &stArrayName);
		}
	}
	GLUseProgram(NULL);

	ShaderGroup** shader = (ShaderGroup**)&shaders;
	DWORD count = sizeof(shaders) / sizeof(ShaderGroup*);
	do
		delete *shader++;
	while (--count);
}

VOID OpenDraw::LoadFilterState()
{
	FilterState state;
	state.interpolation = config.image.interpolation;
	state.upscaling = config.image.upscaling;

	switch (state.upscaling)
	{
	case UpscaleScaleNx:
		state.value = config.image.scaleNx;
		break;

	case UpscaleScaleHQ:
		state.value = config.image.scaleHQ;
		break;

	case UpscaleXRBZ:
		state.value = config.image.xBRz;
		break;

	case UpscaleXSal:
		state.value = config.image.xSal;
		break;

	case UpscaleEagle:
		state.value = config.image.eagle;
		break;

	default:
		state.value = 0;
		break;
	}

	state.flags = TRUE;
	this->filterState = state;
}

VOID OpenDraw::ResetDisplayMode(DWORD width, DWORD height)
{
	OpenDrawSurface* surface = this->attachedSurface;
	if (!this->mode || this->mode->width != width || this->mode->height != height || surface && (surface->width != width || surface->height != height))
	{
		const DisplayMode* mode = modesList;
		DWORD count = sizeof(modesList) / sizeof(DisplayMode);
		do
		{
			if (mode->width == width && mode->height == height)
			{
				this->RenderStop();
				{
					this->mode = mode;
					OpenDrawSurface* surface = this->attachedSurface;
					if (surface)
					{
						surface->ReleaseBuffer();
						surface->CreateBuffer(mode->width, mode->height);
					}
				}
				this->RenderStart();
				return;
			}

			++mode;
		} while (--count);
	}
}

VOID OpenDraw::RenderStart()
{
	if (!this->isFinish || !this->hWnd)
		return;

	this->isFinish = FALSE;

	RECT rect;
	GetClientRect(this->hWnd, &rect);

	if (config.singleWindow)
		this->hDraw = this->hWnd;
	else
	{
		if (this->windowState != WinStateWindowed)
		{
			this->hDraw = CreateWindowEx(
				WS_EX_CONTROLPARENT | WS_EX_TOPMOST,
				WC_DRAW,
				NULL,
				WS_VISIBLE | WS_POPUP,
				0, 0,
				rect.right, rect.bottom,
				this->hWnd,
				NULL,
				hDllModule,
				NULL);
		}
		else
		{
			this->hDraw = CreateWindowEx(
				WS_EX_CONTROLPARENT,
				WC_DRAW,
				NULL,
				WS_VISIBLE | WS_CHILD,
				0, 0,
				rect.right, rect.bottom,
				this->hWnd,
				NULL,
				hDllModule,
				NULL);
		}

		Window::SetCapturePanel(this->hDraw);

		SetClassLongPtr(this->hDraw, GCLP_HBRBACKGROUND, NULL);
		RedrawWindow(this->hDraw, NULL, NULL, RDW_INVALIDATE);
	}

	SetClassLongPtr(this->hWnd, GCLP_HBRBACKGROUND, NULL);
	RedrawWindow(this->hWnd, NULL, NULL, RDW_INVALIDATE);

	this->LoadFilterState();
	this->viewport.width = rect.right;
	this->viewport.height = rect.bottom;
	this->viewport.refresh = TRUE;

	DWORD threadId;
	SECURITY_ATTRIBUTES sAttribs = { sizeof(SECURITY_ATTRIBUTES), NULL, FALSE };
	this->hDrawThread = CreateThread(&sAttribs, NULL, RenderThread, this, NORMAL_PRIORITY_CLASS, &threadId);
}

VOID OpenDraw::RenderStop()
{
	if (this->isFinish)
		return;

	this->isFinish = TRUE;
	SetEvent(this->hDrawEvent);
	WaitForSingleObject(this->hDrawThread, INFINITE);
	CloseHandle(this->hDrawThread);
	this->hDrawThread = NULL;

	if (this->hDraw != this->hWnd)
	{
		DestroyWindow(this->hDraw);
		GL::ResetPixelFormat(this->hWnd);
	}

	this->hDraw = NULL;

	ClipCursor(NULL);

	config.gl.version.value = NULL;
	Window::CheckMenu(this->hWnd);
}

BOOL OpenDraw::CheckView()
{
	if (this->viewport.refresh)
	{
		this->viewport.refresh = FALSE;

		this->viewport.rectangle.x = this->viewport.rectangle.y = 0;
		this->viewport.rectangle.width = this->viewport.width;
		this->viewport.rectangle.height = this->viewport.height;

		this->viewport.clipFactor.x = this->viewport.viewFactor.x = (FLOAT)this->viewport.width / this->mode->width;
		this->viewport.clipFactor.y = this->viewport.viewFactor.y = (FLOAT)this->viewport.height / this->mode->height;

		if (config.image.aspect && this->viewport.viewFactor.x != this->viewport.viewFactor.y)
		{
			if (this->viewport.viewFactor.x > this->viewport.viewFactor.y)
			{
				FLOAT fw = this->viewport.viewFactor.y * this->mode->width;
				this->viewport.rectangle.width = (INT)MathRound(fw);
				this->viewport.rectangle.x = (INT)MathRound(((FLOAT)this->viewport.width - fw) / 2.0f);
				this->viewport.clipFactor.x = this->viewport.viewFactor.y;
			}
			else
			{
				FLOAT fh = this->viewport.viewFactor.x * this->mode->height;
				this->viewport.rectangle.height = (INT)MathRound(fh);
				this->viewport.rectangle.y = (INT)MathRound(((FLOAT)this->viewport.height - fh) / 2.0f);
				this->viewport.clipFactor.y = this->viewport.viewFactor.x;
			}
		}

		HWND hActive = GetForegroundWindow();
		if (config.image.aspect && this->windowState != WinStateWindowed && (hActive == this->hWnd || hActive == this->hDraw))
		{
			RECT clipRect;
			GetClientRect(this->hWnd, &clipRect);

			clipRect.left = this->viewport.rectangle.x;
			clipRect.right = clipRect.left + this->viewport.rectangle.width;
			clipRect.bottom = clipRect.bottom - this->viewport.rectangle.y;
			clipRect.top = clipRect.bottom - this->viewport.rectangle.height;

			ClientToScreen(this->hWnd, (POINT*)&clipRect.left);
			ClientToScreen(this->hWnd, (POINT*)&clipRect.right);

			ClipCursor(&clipRect);
		}
		else
			ClipCursor(NULL);

		return TRUE;
	}

	return FALSE;
}

VOID OpenDraw::ScaleMouse(LPPOINT p)
{
	if (this->viewport.rectangle.width && this->viewport.rectangle.height)
	{
		if (p->x < this->viewport.rectangle.x)
			p->x = 0;
		else if (p->x >= this->viewport.rectangle.x + this->viewport.rectangle.width)
			p->x = this->mode->width - 1;
		else
			p->x = (INT)((FLOAT)(p->x - this->viewport.rectangle.x) / this->viewport.clipFactor.x);

		if (p->y < this->viewport.rectangle.y)
			p->y = 0;
		else if (p->y >= this->viewport.rectangle.y + this->viewport.rectangle.height)
			p->y = this->mode->height - 1;
		else
			p->y = (INT)((FLOAT)(p->y - this->viewport.rectangle.y) / this->viewport.clipFactor.y);
	}
}

OpenDraw::OpenDraw(IDraw7** last)
{
	this->refCount = 1;
	this->last = *last;
	*last = this;

	this->surfaceEntries = NULL;
	this->clipperEntries = NULL;

	this->attachedSurface = NULL;

	this->hWnd = NULL;
	this->hDraw = NULL;
	this->hDc = NULL;

	this->mode = NULL;
	this->isTakeSnapshot = FALSE;
	this->isFinish = TRUE;

	this->hDrawEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

OpenDraw::~OpenDraw()
{
	this->RenderStop();
	CloseHandle(this->hDrawEvent);
	ClipCursor(NULL);
}

ULONG __stdcall OpenDraw::AddRef()
{
	return ++this->refCount;
}

ULONG __stdcall OpenDraw::Release()
{
	if (--this->refCount)
		return this->refCount;

	delete this;
	return 0;
}

HRESULT __stdcall OpenDraw::SetCooperativeLevel(HWND hWnd, DWORD dwFlags)
{
	this->hWnd = hWnd;

	if (dwFlags & DDSCL_FULLSCREEN)
		this->windowState = WinStateFullScreen;
	else
	{
		this->windowState = WinStateWindowed;
		this->RenderStop();
		this->RenderStart();
	}

	return DD_OK;
}

HRESULT __stdcall OpenDraw::EnumDisplayModes(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback)
{
	DDSURFACEDESC2 ddSurfaceDesc;
	MemoryZero(&ddSurfaceDesc, sizeof(DDSURFACEDESC2));

	const DisplayMode* mode = modesList;
	DWORD count = sizeof(modesList) / sizeof(DisplayMode);
	do
	{
		ddSurfaceDesc.dwWidth = mode->width;
		ddSurfaceDesc.dwHeight = mode->height;
		ddSurfaceDesc.ddpfPixelFormat.dwRGBBitCount = mode->bpp;
		ddSurfaceDesc.ddpfPixelFormat.dwRBitMask = 0xF800;
		ddSurfaceDesc.ddpfPixelFormat.dwGBitMask = 0x07E0;
		ddSurfaceDesc.ddpfPixelFormat.dwBBitMask = 0x001F;

		if (!lpEnumModesCallback(&ddSurfaceDesc, NULL))
			break;

		++mode;
	} while (--count);

	return DD_OK;
}

HRESULT __stdcall OpenDraw::SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags)
{
	this->mode = NULL;

	const DisplayMode* mode = modesList;
	DWORD count = sizeof(modesList) / sizeof(DisplayMode);
	do
	{
		if (mode->width == dwWidth && mode->height == dwHeight && mode->bpp == dwBPP)
		{
			this->mode = mode;
			break;
		}

		++mode;
	} while (--count);

	if (!this->mode)
		return DDERR_INVALIDMODE;

	MoveWindow(this->hWnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), TRUE);

	SetForegroundWindow(this->hWnd);

	this->RenderStop();
	this->RenderStart();

	return DD_OK;
}

HRESULT __stdcall OpenDraw::CreateSurface(LPDDSURFACEDESC2 lpDDSurfaceDesc, LPDIRECTDRAWSURFACE7* lplpDDSurface, IUnknown* pUnkOuter)
{
	BOOL isPrimary = lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE;

	this->surfaceEntries = new OpenDrawSurface(this, !isPrimary);
	*lplpDDSurface = (LPDIRECTDRAWSURFACE7)this->surfaceEntries;

	if (isPrimary)
		this->attachedSurface = (OpenDrawSurface*)this->surfaceEntries;

	DWORD width, height;
	if (lpDDSurfaceDesc->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT))
	{
		width = lpDDSurfaceDesc->dwWidth;
		height = lpDDSurfaceDesc->dwHeight;

		((OpenDrawSurface*)this->surfaceEntries)->CreateBuffer(width, height);
	}
	else if (this->windowState != WinStateWindowed)
	{
		width = this->mode->width;
		height = this->mode->height;

		((OpenDrawSurface*)this->surfaceEntries)->CreateBuffer(width, height);
	}

	return DD_OK;
}

HRESULT __stdcall OpenDraw::CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter)
{
	this->clipperEntries = new OpenDrawClipper(this);
	*lplpDDClipper = (LPDIRECTDRAWCLIPPER)this->clipperEntries;

	return DD_OK;
}