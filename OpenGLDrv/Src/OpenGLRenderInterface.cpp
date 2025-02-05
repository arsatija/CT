#include "OpenGLRenderInterface.h"
#include "OpenGLRenderDevice.h"
#include "opengl.h"
#include "OpenGLResource.h"

/*
 * FOpenGLVertexArrayObject
 */

FOpenGLVertexArrayObject::~FOpenGLVertexArrayObject(){
	checkSlow(VAO);
	RenDev->glDeleteVertexArrays(1, &VAO);
}

void FOpenGLVertexArrayObject::Init(UOpenGLRenderDevice* InRenDev, const FStreamDeclaration* Declarations, INT NumStreams){
	checkSlow(VAO == GL_NONE);
	checkSlow(!RenDev);

	RenDev = InRenDev;
	RenDev->glCreateVertexArrays(1, &VAO);

	for(INT StreamIndex = 0; StreamIndex < NumStreams; ++StreamIndex){
		const FStreamDeclaration& Decl = Declarations[StreamIndex];
		GLuint Offset = 0;

		for(INT i = 0; i < Decl.NumComponents; ++i){
			BYTE Function = Decl.Components[i].Function; // EFixedVertexFunction
			BYTE Type     = Decl.Components[i].Type;     // EComponentType

			checkSlow(Function < FVF_MAX);
			checkSlow(Type < CT_MAX);

			switch(Type){
			case CT_Float4:
				RenDev->glVertexArrayAttribFormat(VAO, Function, 4, GL_FLOAT, GL_FALSE, Offset);
				Offset += sizeof(FLOAT) * 4;
				break;
			case CT_Float3:
				RenDev->glVertexArrayAttribFormat(VAO, Function, 3, GL_FLOAT, GL_FALSE, Offset);
				Offset += sizeof(FLOAT) * 3;
				break;
			case CT_Float2:
				RenDev->glVertexArrayAttribFormat(VAO, Function, 2, GL_FLOAT, GL_FALSE, Offset);
				Offset += sizeof(FLOAT) * 2;
				break;
			case CT_Float1:
				RenDev->glVertexArrayAttribFormat(VAO, Function, 1, GL_FLOAT, GL_FALSE, Offset);
				Offset += sizeof(FLOAT);
				break;
			case CT_Color:
				RenDev->glVertexArrayAttribFormat(VAO, Function, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, Offset);
				Offset += sizeof(FColor);
				break;
			default:
				appErrorf("Invalid vertex component type: %i", Type);
			}

			RenDev->glEnableVertexArrayAttrib(VAO, Function);
			RenDev->glVertexArrayAttribBinding(VAO, Function, StreamIndex);
		}
	}
}

void FOpenGLVertexArrayObject::Bind(){
	checkSlow(VAO);
	RenDev->glBindVertexArray(VAO);
}

void FOpenGLVertexArrayObject::BindVertexStreams(FOpenGLVertexStream** Streams, INT NumStreams){
	for(INT i = 0; i < NumStreams; ++i){
		GLuint VBO = Streams[i]->VBO;
		GLuint Stride = Streams[i]->Stride;

		if(VBOs[i] != VBO || Strides[i] != Stride){
			VBOs[i] = VBO;
			Strides[i] = Stride;
			RenDev->glVertexArrayVertexBuffer(VAO, i, VBO, 0, Stride);
		}
	}
}

void FOpenGLVertexArrayObject::BindIndexBuffer(FOpenGLIndexBuffer* IndexBuffer){
	if(EBO != IndexBuffer->EBO){
		EBO = IndexBuffer->EBO;
		RenDev->glVertexArrayElementBuffer(VAO, EBO);
	}
}

/*
 * Helpers
 */

static GLenum GetStencilFunc(/*ECompareFunction*/BYTE Test){
	switch(Test){
	case CF_Never:
		return GL_NEVER;
	case CF_Less:
		return GL_LESS;
	case CF_Equal:
		return GL_EQUAL;
	case CF_LessEqual:
		return GL_LEQUAL;
	case CF_Greater:
		return GL_GREATER;
	case CF_NotEqual:
		return GL_NOTEQUAL;
	case CF_GreaterEqual:
		return GL_GEQUAL;
	case CF_Always:
		return GL_ALWAYS;
	}

	return GL_NEVER;
};

static GLenum GetStencilOp(/*EStencilOp*/BYTE StencilOp){
	switch(StencilOp){
	case SO_Keep:
		return GL_KEEP;
	case SO_Zero:
		return GL_ZERO;
	case SO_Replace:
		return GL_REPLACE;
	case SO_IncrementSat:
		return GL_INCR_WRAP;
	case SO_DecrementSat:
		return GL_DECR_WRAP;
	case SO_Invert:
		return GL_INVERT;
	case SO_Increment:
		return GL_INCR;
	case SO_Decrement:
		return GL_DECR;
	}

	return GL_KEEP;
}

static GLenum GetBlendFunc(/*ED3DBLEND*/BYTE D3DBlend){
	switch(D3DBlend){
	case NOBLEND:
		return GL_ONE;
	case ZERO:
		return GL_ZERO;
	case ONE:
		return GL_ONE;
	case SRCCOLOR:
		return GL_SRC_COLOR;
	case INVSRCCOLOR:
		return GL_ONE_MINUS_SRC_COLOR;
	case SRCALPHA:
		return GL_SRC_ALPHA;
	case INVSRCALPHA:
		return GL_ONE_MINUS_SRC_ALPHA;
	case DESTALPHA:
		return GL_DST_ALPHA;
	case INVDESTALPHA:
		return GL_ONE_MINUS_DST_ALPHA;
	case DESTCOLOR:
		return GL_DST_COLOR;
	case INVDESTCOLOR:
		return GL_ONE_MINUS_DST_COLOR;
	case SRCALPHASAT:
		return GL_SRC_ALPHA_SATURATE;
	case BOTHSRCALPHA:
		check(!"Blend func not implemented"); // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
		return 0;
	case BOTHINVSRCALPHA:
		return GL_ONE_MINUS_SRC_ALPHA;
	}

	return GL_ONE;
}

static GLint GetTextureWrapMode(/*ETexClampMode*/BYTE Mode){
	if(Mode == TC_Clamp)
		return GL_CLAMP_TO_EDGE;

	return GL_REPEAT;
}

/*
 * FOpenGLRenderInterface
 */

FOpenGLRenderInterface::FOpenGLRenderInterface(UOpenGLRenderDevice* InRenDev) : RenDev(InRenDev),
                                                                                PrecacheMode(PRECACHE_All),
																				CurrentState(&SavedStates[0]),
                                                                                LightingOnlyShader(InRenDev),
                                                                                LightingOnlyShader2X(InRenDev),
                                                                                LightingOnlyShaderLightmap(InRenDev),
                                                                                LightingOnlyShaderLightmap2X(InRenDev),
                                                                                BitmapShader(InRenDev),
                                                                                BitmapShaderDetail(InRenDev),
                                                                                BitmapShaderStaticLighting(InRenDev),
                                                                                BitmapShaderStaticLightingDetail(InRenDev),
                                                                                BitmapShaderLightmap(InRenDev),
                                                                                BitmapShaderLightmapDetail(InRenDev),
                                                                                BitmapShaderLightmapStaticLighting(InRenDev),
                                                                                BitmapShaderLightmapStaticLightingDetail(InRenDev),
                                                                                BitmapShaderLightmap2X(InRenDev),
                                                                                BitmapShaderLightmap2XDetail(InRenDev),
                                                                                ParticleShader(InRenDev),
                                                                                ParticleShaderTFactor(InRenDev),
                                                                                ParticleShaderSpecialBlend(InRenDev),
                                                                                ParticleShaderSpecialBlendTFactor(InRenDev),
                                                                                ParticleShaderBlendSubdivisions(InRenDev),
                                                                                TerrainShaderAlphaMap(InRenDev),
                                                                                TerrainShaderAlphaMapStaticLighting(InRenDev),
                                                                                TerrainShader3Layers(InRenDev),
                                                                                TerrainShader4Layers(InRenDev){}

void FOpenGLRenderInterface::Init(INT ViewportWidth, INT ViewportHeight){
	checkSlow(RenDev->IsCurrent());

	// Setup initial state

	RenDev->glEnable(GL_BLEND);
	RenDev->glEnable(GL_DEPTH_TEST);
	RenDev->glEnable(GL_POLYGON_OFFSET_POINT);
	RenDev->glEnable(GL_POLYGON_OFFSET_LINE);
	RenDev->glEnable(GL_POLYGON_OFFSET_FILL);

	// Color

	RenderState.ClearColor = FColor(0, 0, 0, 0);
	RenDev->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// Viewport

	RenderState.ViewportX = 0;
	RenderState.ViewportY = 0;
	RenderState.ViewportWidth = ViewportWidth;
	RenderState.ViewportHeight = ViewportHeight;
	RenDev->glViewport(0, 0, ViewportWidth, ViewportHeight);

	// Fill mode

	RenderState.FillMode = FM_Solid;
	RenDev->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// Culling

	RenDev->glEnable(GL_CULL_FACE);
	RenderState.CullMode = CM_CW;
	LastCullMode = GL_BACK;
	RenDev->glCullFace(LastCullMode);

	// ZTest

	RenderState.bZTest = true;
	RenDev->glDepthFunc(GL_LEQUAL);

	RenderState.bZWrite = true;
	RenDev->glDepthMask(GL_TRUE);

	RenderState.ClearDepth = 1.0f;
	RenDev->glClearDepth(RenderState.ClearDepth);

	// Stencil

	RenDev->glDisable(GL_STENCIL_TEST);

	bStencilEnabled = false;

	RenderState.bStencilTest = false;
	RenderState.StencilCompare = CF_Always;
	RenderState.StencilRef = 0xF;
	RenderState.StencilMask = 0xFF;
	RenderState.StencilFailOp = SO_Keep;
	RenderState.StencilZFailOp = SO_Keep;
	RenderState.StencilPassOp = SO_Keep;
	RenderState.StencilWriteMask = 0xFF;

	RenDev->glStencilOp(GetStencilOp(RenderState.StencilFailOp), GetStencilOp(RenderState.StencilZFailOp), GetStencilOp(RenderState.StencilPassOp));
	RenDev->glStencilFunc(GetStencilFunc(RenderState.StencilCompare), RenderState.StencilRef, RenderState.StencilMask);
	RenDev->glStencilMask(RenderState.StencilWriteMask);

	RenderState.ClearStencil = 0x0;
	RenDev->glClearStencil(RenderState.ClearStencil);

	// Texture samplers

	RenDev->glCreateSamplers(MAX_TEXTURES, Samplers);
	RenDev->glBindSamplers(0, MAX_TEXTURES, Samplers);            // 2D texture samplers
	RenDev->glBindSamplers(MAX_TEXTURES, MAX_TEXTURES, Samplers); // Cubemap samplers

	SetTextureFilter(RenDev->TextureFilter);

	for(int i = 0; i < MAX_TEXTURES; ++i){
		RenDev->glSamplerParameteri(Samplers[i], GL_TEXTURE_WRAP_S, GL_REPEAT);
		RenDev->glSamplerParameteri(Samplers[i], GL_TEXTURE_WRAP_T, GL_REPEAT);
		RenDev->glSamplerParameteri(Samplers[i], GL_TEXTURE_WRAP_R, GL_REPEAT);
		RenderState.TextureUnits[i].ClampU = TC_Wrap;
		RenderState.TextureUnits[i].ClampV = TC_Wrap;
	}

	appMemcpy(static_cast<FOpenGLRenderState*>(CurrentState), &RenderState, sizeof(FOpenGLRenderState));

	// Init uniform default values

	CurrentState->LocalToWorld = FMatrix::Identity;
	CurrentState->WorldToCamera = FMatrix::Identity;
	CurrentState->CameraToScreen = FMatrix::Identity;
	CurrentState->GlobalColor = FPlane(1.0, 1.0, 1.0, 1.0);
	CurrentState->ColorFactor = FPlane(1.0, 1.0, 1.0, 1.0);

	CurrentState->AlphaRef = -1.0f;

	// Create uniform buffer
	RenDev->glCreateBuffers(1, &GlobalUBO);
	RenDev->glNamedBufferStorage(GlobalUBO, sizeof(FOpenGLGlobalUniforms), static_cast<FOpenGLGlobalUniforms*>(CurrentState), GL_DYNAMIC_STORAGE_BIT);
	RenDev->glBindBufferBase(GL_UNIFORM_BUFFER, 0, GlobalUBO); // Binding index 0 is reserved for the global uniform block

	// Initialize default shaders

	FShaderGenerator ShaderGenerator;

	// LightingOnly
	ShaderGenerator.AddColorOp(CA_Diffuse, CA_Diffuse, COP_Arg1, CC_RGBA, CR_0);
	LightingOnlyShader.Compile(*ShaderGenerator.GetShaderText(false), "LightingOnly");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream1);
	ShaderGenerator.AddColorOp(CA_Diffuse, CA_T0, COP_Modulate, CC_RGBA, CR_0);
	LightingOnlyShaderLightmap.Compile(*ShaderGenerator.GetShaderText(false), "LightingOnlyLightmap");
	ShaderGenerator.Reset();
	ShaderGenerator.AddColorOp(CA_Diffuse, CA_Diffuse, COP_Arg1, CC_RGBA, CR_0);
	LightingOnlyShader2X.Compile(*ShaderGenerator.GetShaderText(false), "LightingOnly2X");
	ShaderGenerator.AddTexture(0, TCS_Stream1);
	ShaderGenerator.AddColorOp(CA_R0, CA_T0, COP_Modulate2X, CC_RGB, CR_0);
	LightingOnlyShaderLightmap2X.Compile(*ShaderGenerator.GetShaderText(false), "LightingOnlyLightmap2X");
	// Bitmap
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T0, CA_R0, COP_Arg1, CC_RGBA, CR_0);
	BitmapShader.Compile(*ShaderGenerator.GetShaderText(false), "Bitmap");
	BitmapShaderStaticLighting.Compile(*ShaderGenerator.GetShaderText(true), "BitmapStaticLighting");
	ShaderGenerator.AddTexture(2, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T1, CA_R0, COP_Modulate2X, CC_RGB, CR_0);
	BitmapShaderDetail.Compile(*ShaderGenerator.GetShaderText(false), "BitmapDetail");
	BitmapShaderStaticLightingDetail.Compile(*ShaderGenerator.GetShaderText(true), "BitmapStaticLightingDetail");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddTexture(1, TCS_Stream1);
	ShaderGenerator.AddColorOp(CA_T0, CA_T1, COP_Modulate, CC_RGBA, CR_0);
	BitmapShaderLightmap.Compile(*ShaderGenerator.GetShaderText(false), "BitmapLightmap");
	BitmapShaderLightmapStaticLighting.Compile(*ShaderGenerator.GetShaderText(true), "BitmapLightmapStaticLighting");
	ShaderGenerator.AddTexture(2, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T2, CA_R0, COP_Modulate2X, CC_RGB, CR_0);
	BitmapShaderLightmapDetail.Compile(*ShaderGenerator.GetShaderText(false), "BitmapLightmapDetail");
	BitmapShaderLightmapStaticLightingDetail.Compile(*ShaderGenerator.GetShaderText(true), "BitmapLightmapStaticLightingDetail");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddTexture(1, TCS_Stream1);
	ShaderGenerator.AddColorOp(CA_T0, CA_T1, COP_Modulate2X, CC_RGBA, CR_0);
	BitmapShaderLightmap2X.Compile(*ShaderGenerator.GetShaderText(false), "BitmapLightmap2X");
	ShaderGenerator.AddTexture(2, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T2, CA_R0, COP_Modulate2X, CC_RGB, CR_0);
	BitmapShaderLightmap2XDetail.Compile(*ShaderGenerator.GetShaderText(false), "BitmapLightmap2XDetail");
	// Particle
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T0, CA_Specular, COP_Modulate, CC_RGBA, CR_0);
	ParticleShader.Compile(*ShaderGenerator.GetShaderText(false), "Particle");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T0, CA_GlobalColor, COP_Modulate, CC_RGBA, CR_0);
	ParticleShaderTFactor.Compile(*ShaderGenerator.GetShaderText(false), "ParticleTFactor");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T0, CA_Specular, COP_BlendDiffuseAlpha, CC_RGBA, CR_0);
	ParticleShaderSpecialBlend.Compile(*ShaderGenerator.GetShaderText(false), "ParticleSpecialBlend");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T0, CA_GlobalColor, COP_BlendDiffuseAlpha, CC_RGBA, CR_0);
	ParticleShaderSpecialBlendTFactor.Compile(*ShaderGenerator.GetShaderText(false), "ParticleSpecialBlendTFactor");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddTexture(0, TCS_Stream1);
	ShaderGenerator.AddColorOp(CA_T0, CA_T1, COP_BlendDiffuseAlpha, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_R0, CA_Specular, COP_Modulate, CC_RGB, CR_0);
	ParticleShaderBlendSubdivisions.Compile(*ShaderGenerator.GetShaderText(false), "ParticleBlendSubdivsions");
	// Terrain
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_WorldCoords, TCN_2DCoords, 0);
	ShaderGenerator.AddTexture(1, TCS_Stream0);
	ShaderGenerator.AddColorOp(CA_T0, CA_T0, COP_Arg1, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_T1, CA_T1, COP_Arg1, CC_A, CR_0);
	TerrainShaderAlphaMap.Compile(*ShaderGenerator.GetShaderText(false), "TerrainAlphaMap");
	ShaderGenerator.AddColorOp(CA_R0, CA_Diffuse, COP_Modulate2X, CC_RGB, CR_0);
	TerrainShaderAlphaMapStaticLighting.Compile(*ShaderGenerator.GetShaderText(false), "TerrainAlphaMapStaticLighting");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddTexture(1, TCS_WorldCoords, TCN_2DCoords, 0);
	ShaderGenerator.AddTexture(2, TCS_WorldCoords, TCN_2DCoords, 1);
	ShaderGenerator.AddTexture(3, TCS_WorldCoords, TCN_2DCoords, 2);
	ShaderGenerator.AddColorOp(CA_T1, CA_T0R, COP_Modulate, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_T2, CA_T0G, COP_ModulateAddDest, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_T3, CA_T0B, COP_ModulateAddDest, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_R0, CA_Diffuse, COP_Modulate, CC_RGBA, CR_0);
	TerrainShader3Layers.Compile(*ShaderGenerator.GetShaderText(true), "Terrain3Layers");
	ShaderGenerator.Reset();
	ShaderGenerator.AddTexture(0, TCS_Stream0);
	ShaderGenerator.AddTexture(1, TCS_WorldCoords, TCN_2DCoords, 0);
	ShaderGenerator.AddTexture(2, TCS_WorldCoords, TCN_2DCoords, 1);
	ShaderGenerator.AddTexture(3, TCS_WorldCoords, TCN_2DCoords, 2);
	ShaderGenerator.AddTexture(4, TCS_WorldCoords, TCN_2DCoords, 3);
	ShaderGenerator.AddColorOp(CA_T1, CA_T0R, COP_Modulate, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_T2, CA_T0G, COP_ModulateAddDest, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_T3, CA_T0B, COP_ModulateAddDest, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_T4, CA_T0A, COP_ModulateAddDest, CC_RGBA, CR_0);
	ShaderGenerator.AddColorOp(CA_R0, CA_Diffuse, COP_Modulate, CC_RGBA, CR_0);
	TerrainShader4Layers.Compile(*ShaderGenerator.GetShaderText(true), "Terrain4Layers");
}

void FOpenGLRenderInterface::Flush(){
	checkSlow(CurrentState == &SavedStates[0]);

	CurrentVAO = NULL;
	CurrentIndexBuffer = NULL;
	RenDev->glBindVertexArray(GL_NONE);
	VAOsByDeclId.Empty();

	for(INT i = 0; i < MAX_TEXTURES; ++i)
		CurrentState->TextureUnits[i].Texture = NULL;

	CurrentState->NumTextures = 0;
	CurrentShader = NULL;
	RenDev->glUseProgram(GL_NONE);
}

void FOpenGLRenderInterface::Exit(){
	VAOsByDeclId.Empty();
	RenDev->glDeleteBuffers(1, &GlobalUBO);
	GlobalUBO = GL_NONE;
	RenDev->glDeleteSamplers(MAX_TEXTURES, Samplers);
	appMemzero(Samplers, sizeof(Samplers));
	// Free shaders (just for completeness' sake. Deleting the opengl context also frees those)
	ShadersById.Empty();
	LightingOnlyShader.Free();
	LightingOnlyShader2X.Free();
	LightingOnlyShaderLightmap.Free();
	LightingOnlyShaderLightmap2X.Free();
	BitmapShader.Free();
	BitmapShaderDetail.Free();
	BitmapShaderStaticLighting.Free();
	BitmapShaderStaticLightingDetail.Free();
	BitmapShaderLightmap.Free();
	BitmapShaderLightmapDetail.Free();
	BitmapShaderLightmapStaticLighting.Free();
	BitmapShaderLightmapStaticLightingDetail.Free();
	BitmapShaderLightmap2X.Free();
	BitmapShaderLightmap2XDetail.Free();
	ParticleShader.Free();
	ParticleShaderTFactor.Free();
	ParticleShaderSpecialBlend.Free();
	ParticleShaderSpecialBlendTFactor.Free();
	ParticleShaderBlendSubdivisions.Free();
	TerrainShaderAlphaMap.Free();
	TerrainShaderAlphaMapStaticLighting.Free();
	TerrainShader3Layers.Free();
	TerrainShader4Layers.Free();
}

void FOpenGLRenderInterface::CommitRenderState(){
	checkSlow(CurrentVAO);

	if(CurrentState->FillMode != RenderState.FillMode){
		RenDev->glPolygonMode(GL_FRONT_AND_BACK, CurrentState->FillMode == FM_Wireframe ? GL_LINE : GL_FILL);
		RenderState.FillMode = CurrentState->FillMode;
	}

	if(RenderState.CullMode != CurrentState->CullMode){
		if(CurrentState->CullMode != CM_None){
			GLenum NewCullMode;

			if(CurrentState->CullMode == CM_CCW)
				NewCullMode = GL_FRONT;
			else
				NewCullMode = GL_BACK;

			if(RenderState.CullMode == CM_None)
				RenDev->glEnable(GL_CULL_FACE);

			if(NewCullMode != LastCullMode)
				RenDev->glCullFace(NewCullMode);

			LastCullMode = NewCullMode;
		}else{
			RenDev->glDisable(GL_CULL_FACE);
		}

		RenderState.CullMode = CurrentState->CullMode;
	}

	if(RenderState.bZTest != CurrentState->bZTest){
		RenDev->glDepthFunc(CurrentState->bZTest ? GL_LEQUAL : GL_ALWAYS);
		RenderState.bZTest = CurrentState->bZTest;
	}

	if(RenderState.bZWrite != CurrentState->bZWrite){
		RenDev->glDepthMask(CurrentState->bZWrite ? GL_TRUE : GL_FALSE);
		RenderState.bZWrite = CurrentState->bZWrite;
	}

	if(bStencilEnabled){
		if(RenderState.bStencilTest != CurrentState->bStencilTest){
			CurrentState->StencilCompare = CF_Always;
			CurrentState->StencilFailOp = SO_Keep;
			CurrentState->StencilZFailOp = SO_Keep;
			CurrentState->StencilPassOp = SO_Keep;

			if(CurrentState->bStencilTest)
				CurrentState->StencilWriteMask = 0xFF;
			else
				CurrentState->StencilWriteMask = 0x00;

			RenderState.bStencilTest = CurrentState->bStencilTest;
		}

		if(RenderState.StencilFailOp != CurrentState->StencilFailOp ||
		   RenderState.StencilZFailOp != CurrentState->StencilZFailOp ||
		   RenderState.StencilPassOp != CurrentState->StencilPassOp){
			RenDev->glStencilOp(GetStencilOp(CurrentState->StencilFailOp), GetStencilOp(CurrentState->StencilZFailOp), GetStencilOp(CurrentState->StencilPassOp));
			RenderState.StencilFailOp = CurrentState->StencilFailOp;
			RenderState.StencilZFailOp = CurrentState->StencilZFailOp;
			RenderState.StencilPassOp = CurrentState->StencilPassOp;
		}

		if(RenderState.StencilCompare != CurrentState->StencilCompare ||
		   RenderState.StencilRef != CurrentState->StencilRef ||
		   RenderState.StencilMask != CurrentState->StencilMask){
			RenDev->glStencilFunc(GetStencilFunc(CurrentState->StencilCompare), CurrentState->StencilRef, CurrentState->StencilMask);
			RenderState.StencilCompare = CurrentState->StencilCompare;
			RenderState.StencilRef = CurrentState->StencilRef;
			RenderState.StencilMask = CurrentState->StencilMask;
		}

		if(RenderState.StencilWriteMask != CurrentState->StencilWriteMask){
			RenDev->glStencilMask(CurrentState->StencilWriteMask);
			RenderState.StencilWriteMask = CurrentState->StencilWriteMask;
		}
	}

	if(RenderState.ZBias != CurrentState->ZBias){
		RenDev->glPolygonOffset(-CurrentState->ZBias, -CurrentState->ZBias);
		RenderState.ZBias = CurrentState->ZBias;
	}

	if(RenderState.ViewportX != CurrentState->ViewportX ||
	   RenderState.ViewportY != CurrentState->ViewportY ||
	   RenderState.ViewportWidth != CurrentState->ViewportWidth ||
	   RenderState.ViewportHeight != CurrentState->ViewportHeight){
		RenDev->glViewport(CurrentState->ViewportX, CurrentState->ViewportY, CurrentState->ViewportWidth, CurrentState->ViewportHeight);

		RenderState.ViewportX = CurrentState->ViewportX;
		RenderState.ViewportY = CurrentState->ViewportY;
		RenderState.ViewportWidth = CurrentState->ViewportWidth;
		RenderState.ViewportHeight = CurrentState->ViewportHeight;
	}

	if(RenderState.SrcBlend != CurrentState->SrcBlend || RenderState.DstBlend != CurrentState->DstBlend){
		RenDev->glBlendFunc(CurrentState->SrcBlend, CurrentState->DstBlend);
		RenderState.SrcBlend = CurrentState->SrcBlend;
		RenderState.DstBlend = CurrentState->DstBlend;
	}

	for(INT i = 0; i < CurrentState->NumTextures; ++i){
		if(!CurrentState->TextureUnits[i].Texture) // Texture might not be set if the current material is a hardware shader
			continue;

		if(RenderState.TextureUnits[i].Texture != CurrentState->TextureUnits[i].Texture){
			CurrentState->TextureUnits[i].Texture->BindTexture(i + CurrentState->TextureInfos[i].IsCubemap * MAX_TEXTURES);
			RenderState.TextureUnits[i].Texture = CurrentState->TextureUnits[i].Texture;
		}

		if(RenderState.TextureUnits[i].ClampU != CurrentState->TextureUnits[i].ClampU){
			RenDev->glSamplerParameteri(Samplers[i], GL_TEXTURE_WRAP_S, GetTextureWrapMode(CurrentState->TextureUnits[i].ClampU));
			RenderState.TextureUnits[i].ClampU = CurrentState->TextureUnits[i].ClampU;
		}

		if(RenderState.TextureUnits[i].ClampV != CurrentState->TextureUnits[i].ClampV){
			RenDev->glSamplerParameteri(Samplers[i], GL_TEXTURE_WRAP_T, GetTextureWrapMode(CurrentState->TextureUnits[i].ClampV));
			RenderState.TextureUnits[i].ClampV = CurrentState->TextureUnits[i].ClampV;
		}
	}

	if(CurrentIndexBuffer)
		CurrentVAO->BindIndexBuffer(CurrentIndexBuffer);

	if(LastUniformRevision != CurrentState->UniformRevision){
		if(CurrentState->MatricesChanged)
			UpdateMatrices();

		RenDev->glNamedBufferSubData(GlobalUBO, 0, sizeof(FOpenGLGlobalUniforms), static_cast<FOpenGLGlobalUniforms*>(CurrentState));

		LastUniformRevision = CurrentState->UniformRevision;
	}
}

void FOpenGLRenderInterface::Locked(UViewport* Viewport){
	checkSlow(RenDev->IsCurrent());
	checkSlow(CurrentState == &SavedStates[0]);
	checkSlow(!LockedViewport);

	LockedViewport = Viewport;

	// Detect settings changes in UOpenGLRenderDevice and act accordingly

	if(TextureFilter != RenDev->TextureFilter)
		SetTextureFilter(RenDev->TextureFilter);

	if(TextureAnisotropy != RenDev->TextureAnisotropy){
		TextureAnisotropy = RenDev->TextureAnisotropy;

		if(RenDev->SupportsEXTFilterAnisotropic){
			for(INT i = 0; i < MAX_TEXTURES; ++i)
				RenDev->glSamplerParameterf(Samplers[i], GL_TEXTURE_MAX_ANISOTROPY, Clamp<FLOAT>(TextureAnisotropy, 1, 16));
		}
	}

	if(bStencilEnabled != !!RenDev->UseStencil){
		if(RenDev->UseStencil){
			RenDev->glEnable(GL_STENCIL_TEST);
			bStencilEnabled = true;
		}else{
			RenDev->glDisable(GL_STENCIL_TEST);
			bStencilEnabled = false;
		}
	}

	// Setup per-frame shader constants

	FLOAT Time = appFmod(GEngineTime, 120.0f);

	CurrentState->Time = Time;
	CurrentState->SinTime = appSin(Time);
	CurrentState->CosTime = appCos(Time);
	CurrentState->TanTime = appTan(Time);
}

void FOpenGLRenderInterface::Unlocked(){
	checkSlow(CurrentState == &SavedStates[0]);
	LockedViewport = NULL;
}

void FOpenGLRenderInterface::UpdateMatrices(){
	CurrentState->LocalToCamera = CurrentState->LocalToWorld * CurrentState->WorldToCamera;
	CurrentState->LocalToScreen = CurrentState->LocalToCamera * CurrentState->CameraToScreen;
	CurrentState->WorldToLocal = CurrentState->LocalToWorld.Inverse();
	CurrentState->WorldToScreen = CurrentState->WorldToCamera * CurrentState->CameraToScreen;
	CurrentState->CameraToWorld = CurrentState->WorldToCamera.Inverse();
	CurrentState->MatricesChanged = false;
}

void FOpenGLRenderInterface::SetFramebufferBlending(EFrameBufferBlending Mode){
	switch(Mode){
	case FB_Overwrite:
		CurrentState->SrcBlend = GL_ONE;
		CurrentState->DstBlend = GL_ZERO;
		break;
	case FB_Modulate:
		CurrentState->SrcBlend = GL_DST_COLOR;
		CurrentState->DstBlend = GL_SRC_COLOR;
		break;
	case FB_AlphaBlend:
		CurrentState->SrcBlend = GL_SRC_ALPHA;
		CurrentState->DstBlend = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case FB_AlphaModulate_MightNotFogCorrectly:
		CurrentState->SrcBlend = GL_ONE;
		CurrentState->DstBlend = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case FB_Translucent:
		CurrentState->SrcBlend = GL_ONE;
		CurrentState->DstBlend = GL_ONE;
		break;
	case FB_Darken:
		CurrentState->SrcBlend = GL_ZERO;
		CurrentState->DstBlend = GL_ONE_MINUS_SRC_COLOR;
		break;
	case FB_Brighten:
		CurrentState->SrcBlend = GL_ONE;
		CurrentState->DstBlend = GL_ONE_MINUS_SRC_COLOR;
		break;
	case FB_Invisible:
		CurrentState->SrcBlend = GL_ZERO;
		CurrentState->DstBlend = GL_ONE;
		break;
	case FB_ShadowBlend:
		CurrentState->SrcBlend = GL_SRC_ALPHA;
		CurrentState->DstBlend = GL_ONE;
		break;
	default:
		CurrentState->SrcBlend = GL_ONE;
		CurrentState->DstBlend = GL_ZERO;
	}

}

void FOpenGLRenderInterface::SetTextureFilter(BYTE Filter){
	GLint MinFilter;
	GLint MagFilter;

	switch(Filter){
	case TF_Nearest:
		MinFilter = GL_NEAREST_MIPMAP_NEAREST;
		MagFilter = GL_NEAREST;
		break;
	case TF_Bilinear:
		MinFilter = GL_LINEAR_MIPMAP_NEAREST;
		MagFilter = GL_LINEAR;
		break;
	case TF_Trilinear:
		MinFilter = GL_LINEAR_MIPMAP_LINEAR;
		MagFilter = GL_LINEAR;
		break;
	default:
		return;
	}

	for(INT i = 0; i < MAX_TEXTURES; ++i){
		RenDev->glSamplerParameteri(Samplers[i], GL_TEXTURE_MIN_FILTER, MinFilter);
		RenDev->glSamplerParameteri(Samplers[i], GL_TEXTURE_MAG_FILTER, MagFilter);
	}

	TextureFilter = Filter;
}

void FOpenGLRenderInterface::SetShader(const FOpenGLShader& Shader){
	if(CurrentShader != &Shader){
		Shader.Bind();
		CurrentShader = &Shader;
	}
}

void FOpenGLRenderInterface::PushState(DWORD Flags){
	++CurrentState;

	checkSlow(CurrentState <= &SavedStates[MAX_STATESTACKDEPTH] && "PushState overflow");
	appMemcpy(CurrentState, CurrentState - 1, sizeof(FOpenGLSavedState));
}

void FOpenGLRenderInterface::PopState(DWORD Flags){
	FOpenGLSavedState* PoppedState = CurrentState;

	--CurrentState;

	checkSlow(CurrentState >= &SavedStates[0] && "PopState underflow");

	if((Flags & DONT_RESTORE_RENDER_TARGET) != 0){
		CurrentState->RenderTarget = PoppedState->RenderTarget;
		CurrentState->RenderTargetOwnDepthBuffer = PoppedState->RenderTargetOwnDepthBuffer;
	}else if((Flags & FORCE_RESTORE_RENDER_TARGET) != 0 ||
	         (CurrentState->RenderTarget != PoppedState->RenderTarget ||
	          CurrentState->RenderTargetOwnDepthBuffer != PoppedState->RenderTargetOwnDepthBuffer)){
		SetGLRenderTarget(CurrentState->RenderTarget, CurrentState->RenderTargetOwnDepthBuffer);
	}

	CurrentState->UniformRevision = PoppedState->UniformRevision + 1;
}

UBOOL FOpenGLRenderInterface::SetRenderTarget(FRenderTarget* RenderTarget, bool bOwnDepthBuffer){
	guardFuncSlow;

	checkSlow(RenderTarget);

	QWORD CacheId = RenderTarget->GetCacheId();
	FOpenGLTexture* GLRenderTarget = static_cast<FOpenGLTexture*>(RenDev->GetCachedResource(CacheId));

	if(!GLRenderTarget)
		GLRenderTarget = new FOpenGLTexture(RenDev, CacheId);

	bool NeedsUpdate = false;

	if(GLRenderTarget->Revision != RenderTarget->GetRevision()){
		GLRenderTarget->Cache(RenderTarget, bOwnDepthBuffer);
		NeedsUpdate = true;
	}

	if(NeedsUpdate || GLRenderTarget != CurrentState->RenderTarget || bOwnDepthBuffer != CurrentState->RenderTargetOwnDepthBuffer)
		SetGLRenderTarget(GLRenderTarget, bOwnDepthBuffer);

	return 1;

	unguardSlow;
}

void FOpenGLRenderInterface::SetViewport(INT X, INT Y, INT Width, INT Height){
	CurrentState->ViewportX = X;
	CurrentState->ViewportY = Y;
	CurrentState->ViewportWidth = Width;
	CurrentState->ViewportHeight = Height;
}

void FOpenGLRenderInterface::Clear(UBOOL UseColor, FColor Color, UBOOL UseDepth, FLOAT Depth, UBOOL UseStencil, DWORD Stencil){
	GLbitfield Flags = 0;

	if(UseColor){
		if(RenderState.ClearColor != Color){
			RenDev->glClearColor(Color.R / 255.0f, Color.G / 255.0f, Color.B / 255.0f, Color.A / 255.0f);
			RenderState.ClearColor = Color;
		}

		Flags |= GL_COLOR_BUFFER_BIT;
	}

	if(UseDepth){
		// If ZWrite is disabled, we need to enable it or clearing will have no effect
		if(!RenderState.bZWrite){
			RenDev->glDepthMask(GL_TRUE);
			RenderState.bZWrite = true;
		}

		if(RenderState.ClearDepth != Depth){
			RenDev->glClearDepth(Depth);
			RenderState.ClearDepth = Depth;
		}

		Flags |= GL_DEPTH_BUFFER_BIT;
	}

	if(UseStencil && bStencilEnabled){
		// Same thing as with depth
		if(RenderState.StencilWriteMask != 0xFF){
			RenDev->glStencilMask(0xFF);
			RenderState.StencilWriteMask = 0xFF;
		}

		if(RenderState.ClearStencil != Stencil){
			RenDev->glClearStencil(Stencil);
			RenderState.ClearStencil = Stencil;
		}

		Flags |= GL_STENCIL_BUFFER_BIT;
	}

	RenDev->glClear(Flags);
}

void FOpenGLRenderInterface::SetCullMode(ECullMode CullMode){
	CurrentState->CullMode = CullMode;
}

void FOpenGLRenderInterface::SetAmbientLight(FColor Color){
	CurrentState->AmbientLightColor = Color;
	++CurrentState->UniformRevision;
}

void FOpenGLRenderInterface::EnableLighting(UBOOL UseDynamic, UBOOL UseStatic, UBOOL Modulate2X, FBaseTexture* Lightmap, UBOOL LightingOnly, const FSphere& LitSphere, int){
	CurrentState->UseDynamicLighting = UseDynamic != 0;
	CurrentState->UseStaticLighting = UseStatic != 0;
	CurrentState->LightFactor = Modulate2X ? 2.0f : 1.0f;
	CurrentState->LightingModulate2X = Modulate2X != 0;
	CurrentState->LightingOnly = LightingOnly != 0;
	CurrentState->LitSphere = LitSphere;
	CurrentState->Lightmap = Lightmap;
	++CurrentState->UniformRevision;
}

static FLOAT UnrealAttenuation(FLOAT Distance,FLOAT Radius){
	if(Distance <= Radius){
		FLOAT A = Distance / Radius;
		FLOAT B = (2 * A * A * A - 3 * A * A + 1);

		return B / A * A * 2.0f;
	}

	return 0.0f;
}

void FOpenGLRenderInterface::SetLight(INT LightIndex, FDynamicLight* Light, FLOAT Scale){
	checkSlow(LightIndex < MAX_SHADER_LIGHTS);

	CurrentState->HardwareShaderLights[LightIndex] = Light;

	FOpenGLGlobalUniforms::Light* ShaderLight = &CurrentState->Lights[LightIndex];

	if(Light){
		if(Light->Actor && Light->Actor->LightEffect == LE_Sunlight){
			ShaderLight->Type = SL_Directional;
			ShaderLight->Direction = Light->Direction;
			ShaderLight->Color = Light->Color * 1.75f * Light->Alpha * Scale;
		}else if((Light->Actor && Light->Actor->LightEffect == LE_QuadraticNonIncidence) || CurrentState->LitSphere.W == -1.0f ){
			ShaderLight->Type = SL_Point;
			ShaderLight->Position = Light->Position;
			ShaderLight->Constant = 0.0f;
			ShaderLight->Linear = 0.0f;
			ShaderLight->Quadratic = 8.0f / Square(Light->Radius);
			ShaderLight->Color = Light->Color * Light->Alpha * Scale;
			ShaderLight->Color.W = 1.0f;
		}else{
			ShaderLight->Position = Light->Position;

			// Spotlights.
			if(Light->Actor && Light->Actor->LightCone > 0){
				ShaderLight->Type = SL_Spot;
				ShaderLight->Direction = Light->Direction;
				ShaderLight->Cone = appCos(Light->Actor->LightCone / 256.0f * (float)HALF_PI);

				FLOAT CenterDistance = Max(0.1f, (CurrentState->LitSphere - Light->Position).Size());
				FLOAT MaxDistance = Clamp(CenterDistance + CurrentState->LitSphere.W, Light->Radius * 0.05f, Light->Radius * 0.9f);
				FLOAT MinDistance = Clamp(CenterDistance - CurrentState->LitSphere.W, Light->Radius * 0.1f, Light->Radius * 0.95f);
				FLOAT MinAttenuation = 1.0f / UnrealAttenuation(MinDistance, Light->Radius);
				FLOAT MaxAttenuation = 1.0f / UnrealAttenuation(MaxDistance, Light->Radius);

				if(Abs(MinAttenuation - MaxAttenuation) < SMALL_NUMBER){
					ShaderLight->Constant = MinAttenuation;
					ShaderLight->Linear = 0.0f;
				}else{
					ShaderLight->Constant = Max(0.01f, MinAttenuation - (MaxAttenuation - MinAttenuation) / (MaxDistance - MinDistance) * MinDistance);
					ShaderLight->Linear = Max(0.0f, (MinAttenuation - ShaderLight->Constant) / MinDistance);
				}

				ShaderLight->Quadratic = 0.0f;
			}else{ // Point lights.
				ShaderLight->Type = SL_Point;

				FLOAT CenterDistance = Max(0.1f, (CurrentState->LitSphere - Light->Position).Size());
				FLOAT MaxDistance = Clamp(CenterDistance + CurrentState->LitSphere.W, Light->Radius * 0.05f, Light->Radius * 0.9f);
				FLOAT MinDistance = Clamp(CenterDistance - CurrentState->LitSphere.W, Light->Radius * 0.1f, Light->Radius * 0.95f);
				FLOAT MinAttenuation = 1.0f / UnrealAttenuation(MinDistance, Light->Radius);
				FLOAT MaxAttenuation = 1.0f / UnrealAttenuation(MaxDistance, Light->Radius);

				if(Abs(MinAttenuation - MaxAttenuation) < SMALL_NUMBER){
					ShaderLight->Constant = MinAttenuation;
					ShaderLight->Linear = 0.0f;
				}else{
					ShaderLight->Constant= Max(0.01f, MinAttenuation - (MaxAttenuation - MinAttenuation) / (MaxDistance - MinDistance) * MinDistance);
					ShaderLight->Linear= Max(0.0f, (MinAttenuation - ShaderLight->Constant) / MinDistance);
				}

				ShaderLight->Quadratic = 0.0f;
			}

			ShaderLight->Color = Light->Color * Light->Alpha * Scale;
			ShaderLight->Color.W = 1.0f;
		}

		ShaderLight->Constant = Max(0.0f, ShaderLight->Constant);
		ShaderLight->Linear = Max(0.0f, ShaderLight->Linear);
		ShaderLight->Quadratic = Max(0.0f, ShaderLight->Quadratic);

		if(CurrentState->LightingModulate2X)
			ShaderLight->Color *= 0.5f;
	}else{
		appMemzero(ShaderLight, sizeof(*ShaderLight));
		ShaderLight->Position = FVector(10000000.0f);
	}

	++CurrentState->UniformRevision;
}

void FOpenGLRenderInterface::SetDistanceFog(UBOOL Enable, FLOAT FogStart, FLOAT FogEnd, FColor Color){
	CurrentState->FogEnabled = Enable;
	CurrentState->FogStart = FogStart;
	CurrentState->FogEnd = FogEnd;

	if(CurrentState->OverrideFogColor)
		CurrentState->SavedFogColor = Color;
	else
		CurrentState->FogColor = Color;

	++CurrentState->UniformRevision;
}

UBOOL FOpenGLRenderInterface::EnableFog(UBOOL Enable){
	CurrentState->FogEnabled = Enable != 0;
	++CurrentState->UniformRevision;

	return Enable;
}

UBOOL FOpenGLRenderInterface::IsFogEnabled(){
	return CurrentState->FogEnabled;
}

void FOpenGLRenderInterface::SetGlobalColor(FColor Color){
	CurrentState->GlobalColor = Color;
	++CurrentState->UniformRevision;
}

void FOpenGLRenderInterface::SetTransform(ETransformType Type, const FMatrix& Matrix){
	/*
	 * HACK:
	 * GIsOpenGL needs to be set to 0. However, it is checked for in UCanvas::Update which applies a 0.5 pixel offset for d3d.
	 * Here we check if the given matrix belongs to the viewport's canvas and remove the offset.
	 */
	if(&Matrix == &LockedViewport->Canvas->pCanvasUtil->CanvasToScreen){
		FMatrix& Mat = const_cast<FMatrix&>(Matrix);

		Mat.M[3][0] = static_cast<INT>(Mat.M[3][0]);
		Mat.M[3][1] = static_cast<INT>(Mat.M[3][1]);
	}

	switch(Type){
	case TT_LocalToWorld:
		CurrentState->LocalToWorld = Matrix;
		break;
	case TT_WorldToCamera:
		CurrentState->WorldToCamera = Matrix;
		break;
	case TT_CameraToScreen:
		CurrentState->CameraToScreen = Matrix;
	}

	CurrentState->MatricesChanged = true;
	++CurrentState->UniformRevision;
}

FMatrix FOpenGLRenderInterface::GetTransform(ETransformType Type) const{
	switch(Type){
	case TT_LocalToWorld:
		return CurrentState->LocalToWorld;
	case TT_WorldToCamera:
		return CurrentState->WorldToCamera;
	case TT_CameraToScreen:
		return CurrentState->CameraToScreen;
	}

	return FMatrix::Identity;
}

void FOpenGLRenderInterface::SetMaterial(UMaterial* Material, FString* ErrorString, UMaterial** ErrorMaterial, INT* NumPasses){
	guardFuncSlow;

	// Use default material if precaching geometry

	if(!Material || PrecacheMode == PRECACHE_VertexBuffers)
		Material = GetDefault<UMaterial>()->DefaultMaterial;

	/*
	 * HACK:
	 * FrameFX render targets are offset by -0.5 pixels which is needed for Direct3D but causes a blurry image with OpenGL.
	 * This is fixed by checking whether the current material is one of the FrameFX shaders and removing the offset from the CameraToScreen matrix.
	 */
	UFrameFX* FrameFX = LockedViewport->Actor->FrameFX;
	if((FrameFX && (Material == FrameFX->ShaderDraw ||
	                Material == FrameFX->ShaderGlow ||
	                Material == FrameFX->ShaderBlur ||
	                (FrameFX->VisionMode && Material == FrameFX->VisionMode->VisionShader)))){
		// Remove offset by truncating float to int.
		// This works because the x/y values will only ever be -1 or 1 (plus fractional offset) for fullscreen quads or 0 for FFrameGrid
		CurrentState->CameraToScreen.M[3][0] = static_cast<INT>(CurrentState->CameraToScreen.M[3][0]);
		CurrentState->CameraToScreen.M[3][1] = static_cast<INT>(CurrentState->CameraToScreen.M[3][1]);
	}

#if 0
	// Check for circular references

	if(GIsEditor){
		TArray<UMaterial*> History;

		if(!Material->CheckCircularReferences(History)){
			if(History.Num() >= 0){
				if(ErrorString)
					*ErrorString = FString::Printf("Circular material reference in '%s'", History.Last()->GetName());

				if(ErrorMaterial)
					*ErrorMaterial = History.Last();
			}

			return;
		}
	}

	if(NumPasses)
		*NumPasses = 1;
#endif

	// Restore default material state

	CurrentState->bZTest = true;
	CurrentState->bZWrite = true;
	CurrentState->SrcBlend = GL_ONE;
	CurrentState->DstBlend = GL_ZERO;
	CurrentState->NumTextures = 0;

	if(CurrentState->AlphaRef >= 0.0f || CurrentState->ModifyColor){
		CurrentState->AlphaRef = -1.0f;
		CurrentState->ModifyColor = 0;
		CurrentState->ColorFactor = FPlane(1.0f, 1.0f, 1.0f, 1.0f);
		++CurrentState->UniformRevision;
	}

	if(CurrentState->OverrideFogColor){
		CurrentState->FogColor = CurrentState->SavedFogColor;
		CurrentState->OverrideFogColor = false;
		++CurrentState->UniformRevision;
	}

	ModifyFramebufferBlending = false;
	UsingLightmap = false;
	NumTexMatrices = 0;

	Material->PreSetMaterial(GEngineTime);

	UModifier* Modifier = Cast<UModifier>(Material);
	FModifierInfo ModifierInfo;

	if(Modifier){
		Material = RemoveModifiers(Cast<UModifier>(Material), ModifierInfo);

		if(!Material){
			SetShader(RenDev->ErrorShader);

			if(ErrorString)
				*ErrorString = "Modifier does not have a material";

			if(ErrorMaterial)
				*ErrorMaterial = Modifier;

			return;
		}
	}

	if(CurrentState->LightingOnly){
		FOpenGLShader* Shader;

		if(CurrentState->Lightmap){
			SetTexture(CurrentState->Lightmap, 0);

			if(CurrentState->LightingModulate2X)
				Shader = &LightingOnlyShaderLightmap2X;
			else
				Shader = &LightingOnlyShaderLightmap;
		}else{
			if(CurrentState->LightingModulate2X)
				Shader = &LightingOnlyShader2X;
			else
				Shader = &LightingOnlyShader;
		}

		SetShader(*Shader);

		return;
	}

	bool Result = false;

	if(Material->IsA<UHardwareShaderWrapper>()){
		Result = static_cast<UHardwareShaderWrapper*>(Material)->SetupShaderWrapper(this) != 0;
	}else if(Material->IsA<UHardwareShader>()){
		Result = SetHardwareShaderMaterial(static_cast<UHardwareShader*>(Material), ErrorString, ErrorMaterial) != 0;
	}else if(Material->IsA<UProjectorMultiMaterial>()){
		Result = SetProjectorMultiMaterial(static_cast<UProjectorMultiMaterial*>(Material));
	}else{
		const FOpenGLShader* Shader = NULL;

		if(Modifier)
			Shader = RenDev->GetShaderForMaterial(Modifier);

		if(!Shader)
			Shader = RenDev->GetShaderForMaterial(Material);

		if(Material->IsA<UBitmapMaterial>())
			Result = ModifierInfo.bModifiesTextureCoordinates ? SetSimpleMaterial(Material, ModifierInfo) : SetBitmapMaterial(static_cast<UBitmapMaterial*>(Material), ModifierInfo);
		else if(Material->IsA<UShader>())
			Result = SetShaderMaterial(static_cast<UShader*>(Material), ModifierInfo);
		else if(Material->IsA<UCombiner>())
			Result = SetCombinerMaterial(static_cast<UCombiner*>(Material));
		else if(Material->IsA<UParticleMaterial>())
			Result = SetParticleMaterial(static_cast<UParticleMaterial*>(Material));
		else if(Material->IsA<UProjectorMaterial>())
			Result = SetProjectorMaterial(static_cast<UProjectorMaterial*>(Material));
		else if(Material->IsA<UTerrainMaterial>())
			Result = SetTerrainMaterial(static_cast<UTerrainMaterial*>(Material));
		else
			Result = SetSimpleMaterial(Material, ModifierInfo);

		if(Shader){
			if(Shader->IsValid())
				SetShader(*Shader);
			else
				Result = false;
		}
	}

	if(!Result)
		SetShader(RenDev->ErrorShader);

	unguardSlow;
}

static INT GetShaderConstantNumSlots(BYTE ConstantType){
	if((ConstantType >= EVC_WorldToScreenMatrix && ConstantType <= EVC_WorldToObjectMatrix) || ConstantType == EVC_ObjectToCameraMatrix)
		return 4;

	if(ConstantType == EVC_2DRotator)
		return 2;

	return 1;
}

UBOOL FOpenGLRenderInterface::SetHardwareShaderMaterial(UHardwareShader* HardwareShader, FString* ErrorString, UMaterial** ErrorMaterial){
	guardFuncSlow;

	const FOpenGLShader* Shader = RenDev->GetShaderForMaterial(HardwareShader);

	if(Shader && Shader->IsValid()){
		SetShader(*Shader);

		CurrentState->bZTest = HardwareShader->ZTest != 0;
		CurrentState->bZWrite = HardwareShader->ZWrite != 0;

		if(HardwareShader->AlphaTest)
			CurrentState->AlphaRef = HardwareShader->AlphaRef / 255.0f;

		if(HardwareShader->AlphaBlending){
			CurrentState->SrcBlend = GetBlendFunc(HardwareShader->SrcBlend);
			CurrentState->DstBlend = GetBlendFunc(HardwareShader->DestBlend);
		}

		for(INT i = 0; i < MAX_TEXTURES; ++i){
			if(HardwareShader->Textures[i])
				SetBitmapTexture(HardwareShader->Textures[i], i, 1.0f, TCO_UseTextureMode, TCO_UseTextureMode, HardwareShader->BumpSettings[i].BumpSize);
		}

		// Cache number of vertex and pixel shader constants

		if(HardwareShader->NumVSConstants <= 0){
			for(INT i = MAX_VERTEX_SHADER_CONSTANTS - 1; i >= 0; --i){
				if(HardwareShader->VSConstants[i].Type != EVC_Unused){
					HardwareShader->NumVSConstants = i + GetShaderConstantNumSlots(HardwareShader->VSConstants[i].Type);
					break;
				}
			}
		}

		if(HardwareShader->NumPSConstants <= 0){
			for(INT i = MAX_PIXEL_SHADER_CONSTANTS - 1; i >= 0; --i){
				if(HardwareShader->PSConstants[i].Type != EVC_Unused){
					HardwareShader->NumPSConstants = i + GetShaderConstantNumSlots(HardwareShader->PSConstants[i].Type);
					break;
				}
			}
		}

		GetShaderConstants(HardwareShader->VSConstants, ShaderConstants, HardwareShader->NumVSConstants);
		RenDev->glProgramUniform4fv(CurrentShader->Program, 0, HardwareShader->NumVSConstants, (GLfloat*)ShaderConstants);
		GetShaderConstants(HardwareShader->PSConstants, ShaderConstants, HardwareShader->NumPSConstants);
		RenDev->glProgramUniform4fv(CurrentShader->Program, MAX_VERTEX_SHADER_CONSTANTS, HardwareShader->NumPSConstants, (GLfloat*)ShaderConstants);

		return 1;
	}

	return 0;

	unguardSlow;
}

void FOpenGLRenderInterface::SetGLRenderTarget(FOpenGLTexture* GLRenderTarget, bool bOwnDepthBuffer){
	checkSlow(GLRenderTarget);

	GLRenderTarget->BindRenderTarget();
	SetViewport(0, 0, GLRenderTarget->Width, GLRenderTarget->Height);

	CurrentState->RenderTarget = GLRenderTarget;
	CurrentState->RenderTargetOwnDepthBuffer = bOwnDepthBuffer;
}

void FOpenGLRenderInterface::CopyBackBufferToTarget(FAuxRenderTarget* Target){
	FOpenGLTexture* GLTarget = static_cast<FOpenGLTexture*>(RenDev->GetCachedResource(Target->GetCacheId()));
	FOpenGLTexture* Backbuffer = static_cast<FOpenGLTexture*>(RenDev->GetCachedResource(RenDev->Backbuffer.GetCacheId()));

	if(!GLTarget || !Backbuffer)
		return;

	GLbitfield Flags = GL_COLOR_BUFFER_BIT;

	if(GLTarget->DepthStencilAttachment && !GLTarget->HasSharedDepthStencil)
		Flags |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;

	RenDev->glBlitNamedFramebuffer(Backbuffer->FBO,
	                               GLTarget->FBO,
	                               0, 0, Backbuffer->Width, Backbuffer->Height,
	                               0, 0, GLTarget->Width, GLTarget->Height,
	                               Flags,
	                               GL_NEAREST);
}

void FOpenGLRenderInterface::SetStencilOp(ECompareFunction Test, DWORD Ref, DWORD Mask, EStencilOp FailOp, EStencilOp ZFailOp, EStencilOp PassOp, DWORD WriteMask){
	CurrentState->StencilCompare = Test;
	CurrentState->StencilRef = static_cast<BYTE>(Ref);
	CurrentState->StencilMask = static_cast<BYTE>(Mask);
	CurrentState->StencilFailOp = FailOp;
	CurrentState->StencilZFailOp = ZFailOp;
	CurrentState->StencilPassOp = PassOp;
	CurrentState->StencilWriteMask = static_cast<BYTE>(WriteMask);
}

void FOpenGLRenderInterface::EnableStencil(UBOOL Enable){
	CurrentState->bStencilTest = Enable != 0;
}

void FOpenGLRenderInterface::EnableDepth(UBOOL Enable){
	if(Enable){
		CurrentState->bZWrite = true;
		CurrentState->bZTest = true;
	}else{
		CurrentState->bZWrite = false;
		CurrentState->bZTest = false;
	}
}

void FOpenGLRenderInterface::SetZBias(INT ZBias){
	CurrentState->ZBias = ZBias;
}

INT FOpenGLRenderInterface::SetVertexStreams(EVertexShader Shader, FVertexStream** Streams, INT NumStreams){
	guardFuncSlow;

	FStreamDeclaration VertexStreamDeclarations[MAX_VERTEX_STREAMS];
	FOpenGLVertexStream* OpenGLStreams[MAX_VERTEX_STREAMS];

	// NOTE: Stream declarations must be completely zeroed to get consistent hash values when looking up the VAO later
	appMemzero(VertexStreamDeclarations, sizeof(VertexStreamDeclarations));

	INT Size = 0;

	for(INT i = 0; i < NumStreams; ++i){
		QWORD CacheId = Streams[i]->GetCacheId();
		FOpenGLVertexStream* Stream;

		Stream = static_cast<FOpenGLVertexStream*>(RenDev->GetCachedResource(CacheId));

		if(!Stream)
			Stream = new FOpenGLVertexStream(RenDev, CacheId);

		if(Stream->Revision != Streams[i]->GetRevision()){
			Stream->Cache(Streams[i]);
			Size += Stream->BufferSize;
		}

		OpenGLStreams[i] = Stream;
		VertexStreamDeclarations[i].Init(Streams[i]);
	}

	// Look up VAO by format
	// Check if there is an existing VAO for this format by hashing the stream declarations
	FOpenGLVertexArrayObject& VAO = VAOsByDeclId[appMemhash(VertexStreamDeclarations, sizeof(FStreamDeclaration) * NumStreams)];

	if(!VAO.IsValid())
		VAO.Init(RenDev, VertexStreamDeclarations, NumStreams);

	VAO.BindVertexStreams(OpenGLStreams, NumStreams);

	if(&VAO != CurrentVAO){
		VAO.Bind();
		CurrentVAO = &VAO;
	}

	VertexBufferBase = 0;

	return Size;

	unguardSlow;
}

INT FOpenGLRenderInterface::SetDynamicStream(EVertexShader Shader, FVertexStream* Stream){
	guardFuncSlow;

	FOpenGLVertexStream* OpenGLStream = RenDev->GetDynamicVertexStream();

	FStreamDeclaration VertexStreamDeclaration;
	appMemzero(&VertexStreamDeclaration, sizeof(VertexStreamDeclaration));

	VertexStreamDeclaration.Init(Stream);

	// Look up VAO by format
	// Check if there is an existing VAO for this format by hashing the stream declarations
	FOpenGLVertexArrayObject& VAO = VAOsByDeclId[appMemhash(&VertexStreamDeclaration, sizeof(FStreamDeclaration))];

	if(!VAO.IsValid())
		VAO.Init(RenDev, &VertexStreamDeclaration, 1);

	VertexBufferBase = OpenGLStream->AddVertices(Stream);

	VAO.BindVertexStreams(&OpenGLStream, 1);

	if(&VAO != CurrentVAO){
		VAO.Bind();
		CurrentVAO = &VAO;
	}

	return 0;

	unguardSlow;
}

INT FOpenGLRenderInterface::SetIndexBuffer(FIndexBuffer* IndexBuffer, INT BaseIndex){
	guardFuncSlow;

	bool RequiresCaching = false;

	if(IndexBuffer){
		FOpenGLIndexBuffer* Buffer;
		INT IndexSize = IndexBuffer->GetIndexSize();

		checkSlow(IndexSize == sizeof(_WORD) || IndexSize == sizeof(DWORD));

		QWORD CacheId = IndexBuffer->GetCacheId();

		Buffer = static_cast<FOpenGLIndexBuffer*>(RenDev->GetCachedResource(CacheId));

		if(!Buffer)
			Buffer = new FOpenGLIndexBuffer(RenDev, CacheId);

		if(Buffer->Revision != IndexBuffer->GetRevision() || Buffer->IndexSize != IndexSize){
			RequiresCaching = true;
			Buffer->Cache(IndexBuffer);
		}

		IndexBufferBase = BaseIndex;
		CurrentIndexBuffer = Buffer;
	}else{
		IndexBufferBase = 0;
		CurrentIndexBuffer = NULL;
	}

	return RequiresCaching ? IndexBuffer->GetSize() : 0;

	unguardSlow;
}

INT FOpenGLRenderInterface::SetDynamicIndexBuffer(FIndexBuffer* IndexBuffer, INT BaseIndex){
	guardFuncSlow;

	FOpenGLIndexBuffer* Buffer = RenDev->GetDynamicIndexBuffer(IndexBuffer->GetIndexSize());

	IndexBufferBase = BaseIndex;
	CurrentIndexBuffer = Buffer;

	return Buffer->AddIndices(IndexBuffer);

	unguardSlow;
}

void FOpenGLRenderInterface::DrawPrimitive(EPrimitiveType PrimitiveType, INT FirstIndex, INT NumPrimitives, INT MinIndex, INT MaxIndex){
	guardFuncSlow;

	CommitRenderState();

	GLenum Mode  = 0;
	INT    Count = NumPrimitives;

	switch(PrimitiveType){
	case PT_PointList:
		Mode = GL_POINTS;
		break;
	case PT_LineList:
		Mode = GL_LINES;
		Count *= 2;
		break;
	case PT_TriangleList:
		Mode = GL_TRIANGLES;
		Count *= 3;
		break;
	case PT_TriangleStrip:
		Mode = GL_TRIANGLE_STRIP;
		Count += 2;
		break;
	case PT_TriangleFan:
		Mode = GL_TRIANGLE_FAN;
		Count += 2;
		break;
	default:
		appErrorf("Invalid primitive type for FOpenGLRenderInterface::DrawPrimitive: %i", PrimitiveType);
	};

	if(CurrentIndexBuffer){
		INT IndexSize = CurrentIndexBuffer->IndexSize;

		RenDev->glDrawRangeElementsBaseVertex(Mode,
		                                      MinIndex,
		                                      MaxIndex,
		                                      Count,
		                                      IndexSize == sizeof(_WORD) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
		                                      reinterpret_cast<void*>((FirstIndex + IndexBufferBase) * IndexSize),
		                                      VertexBufferBase);
	}else{
		RenDev->glDrawArrays(Mode, VertexBufferBase + FirstIndex, Count);
	}

	unguardSlow;
}

void FOpenGLRenderInterface::SetFillMode(EFillMode FillMode){
	CurrentState->FillMode = FillMode;
}
