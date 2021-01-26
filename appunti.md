# Appunti UE4

UE4 Separa le operazioni di rendering in un thread a parte, con la sua 
rappresentazione del mondo (ossia esistono un Game Thread ed un Rendering Thread).

Gli oggetti che estendono UPrimitiveComponent sono in grado o di essere renderizzati in
qualche modo, o di interagire con la luce, e hanno un FPrimitiveSceneProxy che li rappresenta
nel Rendering Thread (es. UStaticMeshComponent, UCableComponent, USkeletalMeshComponent...)

FPrimitiveSceneProxy estende vari metodi, quelli importanti (se ho capito bene) sono 3:
* GetViewRelevance(), che indica restituisce una FPrimitiveViewRelevance che descrive la primitiva
da renderizzare (es. se usa il Dynamic path, ossia i suoi vertici cambiano spesso da frame a frame, 
o se usa lo Static path, ossia i suoi vertici cambiano poco);
* DrawStaticElements() che disegna mesh tramite una FStaticPrimitiveDrawInterface*, e viene chiamato se si usa lo static path;
* GetDynamicMeshElements, che viene chiamato nel dynamic path per oggetti dinamici.

In entrami i casi, si interagisce con delle FMeshBatch, ossia delle rappresentazioni di una mesh che **devono** contenere (almeno):
* una Vertex Factory, che genera vertici (credo tipo moltiplicandoli per la matrice MVP oppure in altri modi);
* un MaterialRenderProxy, ossia un'interfaccia per un UMaterial (Quelli di default sono ottenibili tramite UMaterial::GetDefaultMaterial());
Inoltre gli FMeshBatch sono composti in FMeshBatchElement (FMeshBatch::Elements è un TArray<FMeshBatchElement>), se ho capito bene in genere si usa solo il primo Element, e ogni Element deve contenere un riferimento ad un FIndexBuffer nel campo IndexBuffer, più altre informazioni sull'elemento (il primo indice, il vertice di indice massimo etc...)

TODO: CAPIRE COME CRAXO FUNZIONANO UStaticMeshComponent, UCableComponent ETC...

### UStaticMeshComponent

UStaticMeshComponent contiene un puntatore user-settabile ad UStaticMesh
Struttura:
`TArray<FStaticMeshSourceModel>` contenente i dati grezzi dai quali sono costruiti i vari LOD della mesh
`TArray<FStaticMeshRenderData>` contenente i dati che si usano per effettuare il rendering della mesh (e che vengono usati dal proxy di UStaticMeshComponent)

### Struttura di FStaticMeshRenderData
A noi ci interessano `FStaticMeshLodResourcesArray = TIndirectArray<FStaticMeshLODResources>`
e `FStaticMeshVertexFactoriesArray = TArray<FStaticMeshVertexFactories>`

FStaticMeshLODResources
> Rendering resources needed to render an individual static mesh LOD

A sua volta è composta da FStaticMeshSection (DIOBONO QUANTI LIVELLI)
```c++
using FStaticMeshSectionArray = TArray<FStaticMeshSection, TInlineAllocator<1>>;
FStaticMeshSectionArray Sections;
```
> A set of static mesh triangles which are rendered with the same material.

```c++
/** The index of the material with which to render this section. */
int32 MaterialIndex;

/** Range of vertices and indices used when rendering this section. */
uint32 FirstIndex;
uint32 NumTriangles;
uint32 MinVertexIndex;
uint32 MaxVertexIndex;
```
Se ho capito bene, non fa altro che mantenere gli indici che compongono una sezione della mesh (non vedo roba simile a vertici)

Ogni LOD hai seguenti campi:
```c++
// Dove 
FStaticMeshVertexBuffers VertexBuffers;
struct FStaticMeshVertexBuffers
{
	/** The buffer containing vertex data. */
	FStaticMeshVertexBuffer StaticMeshVertexBuffer;
    // Estende FRenderResource, contiene i vertici
	/** The buffer containing the position vertex data. */
	FPositionVertexBuffer PositionVertexBuffer;
    // Estende FVertexBuffer, contiene le posizioni dei vertici (qual'è la differenza con StaticMeshVertexBuffer?)
	/** The buffer containing the vertex color data. */
	FColorVertexBuffer ColorVertexBuffer;
    // Estende FVertexBuffer
	/* This is a temporary function to refactor and convert old code, do not copy this as is and try to build your data as SoA from the beginning.*/
	void ENGINE_API InitWithDummyData(FLocalVertexFactory* VertexFactory, uint32 NumVerticies, uint32 NumTexCoords = 1, uint32 LightMapIndex = 0);

	/* This is a temporary function to refactor and convert old code, do not copy this as is and try to build your data as SoA from the beginning.*/
	void ENGINE_API InitFromDynamicVertex(FLocalVertexFactory* VertexFactory, TArray<FDynamicMeshVertex>& Vertices, uint32 NumTexCoords = 1, uint32 LightMapIndex = 0);

	/* This is a temporary function to refactor and convert old code, do not copy this as is and try to build your data as SoA from the beginning.*/
	void ENGINE_API InitModelBuffers(TArray<FModelVertex>& Vertices);

	/* This is a temporary function to refactor and convert old code, do not copy this as is and try to build your data as SoA from the beginning.*/
	void ENGINE_API InitModelVF(FLocalVertexFactory* VertexFactory);

    ...

	class FTangentsVertexBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FTangentsVertexBuffer"); }
	} TangentsVertexBuffer;

	class FTexcoordVertexBuffer : public FVertexBuffer
	{
		virtual FString GetFriendlyName() const override { return TEXT("FTexcoordVertexBuffer"); }
	} TexCoordVertexBuffer;

};


/** Index buffer resource for rendering. */
FRawStaticIndexBuffer IndexBuffer;
// Estende FIndexBuffer, contiene gli indici (AGAIN MA QUINDI A CHE SERVONO GLI FStaticMeshSection??!?!?)
```

A posto, dopo 35050050 struct diverse, si arriva al bread and butter del rendering (forse):
Premessa: Per forzare l'esecuzione di blocchi di codice sul Rendering Thread, si usa
```
ENQUEUE_RENDER_COMMAND(NomeComando)(
		[](FRHICommandListImmediate& RHICmdList)
		{
            Codice
		});
```
`FStaticMeshLODResources::InitResources(UStaticMesh* Parent)`, riassumibile con:
```c++
BeginInitResource(&InitBuffer) // Tramite ENQUEUE_RENDER_COMMAND() si inizializza l'index buffer, chiamando il suo InitResource(),
// ossia che chiama InitDynamicRHI e InitRHI sulla resource (fa anche altro, ma la roba
// importante è questa)
/*
InitRHI() di FindexBuffer non fa altro che creare un Index Buffer sul render thread tramite 
// RHICreateIndexBuffer o RHIAsyncCreateIndexBuffer (quando non sta sul Render Thread)
*/

BeginInitResource(&VertexBuffers.StaticMeshVertexBuffer); 
/*
InitResource di FStaticMeshVertexBuffer si comporta in modo diverso:
1) Chiama FRenderResource::InitResource(), quindi InitRHI(), che inizializzano
    TangentsVertexBuffer e TexCoordVertexBuffer.
    Vengono inoltre inizializzati TangentsSRV e TextureCoordinatesSRV tramite RHICreateShaderResourceView
    Dalla Microsoft cosonetwork:
        Shader resource views typically wrap textures in a format that the shaders can access them.
    Ancora non ho capito che sono.
    Chuck Walbourn su StackOverflow alla riscossa:
        You have to have an SRV to bind a resource to a shader for read. You must have a RTV to bind a resource to the render pipeline output.
    Quindi sono obbligatorie
2) Chiama TangentsVertexBuffer.InitResource e TexCoordVertexBuffer.InitResource(), ossia FVertexBuffer::InitResource(), dato che non è stato fatto l'override, viene chiamato FRenderResource::InitResource(), che chiama quindi InitRHI di FRenderResource, che non fa niente (credo sia stato fatto in caso ci sia bisogno di aggiungere qualcosa a FVertexBuffer).
Quindi i FVertexBuffer vanno inizializzati a mano (cosa che viene fatta al passo sopra)
*/
BeginInitResource(&VertexBuffers.PositionVertexBuffer);
//FPositionVertxBuffer::InitRHI() inizializza sia il VertexBufferRHI che il PositionComponentSRV
```

Fin'ora, se ho capito bene, vanno creati (sul render thread) dei FVertexBuffer (con relativi SRV, creati con nullptr se non ci sono dati), uno per posizioni dei vertici, vertici, colori, tangenti e uv (TexCoords). 
La parte difficile è creare i SRV

Fatto questo, tramite InitVertexFactory si crea la vertex factory:

```c++

void FStaticMeshVertexFactories::InitVertexFactory(
	const FStaticMeshLODResources& LodResources,
	FLocalVertexFactory& InOutVertexFactory,
	uint32 LODIndex,
	const UStaticMesh* InParentMesh,
	bool bInOverrideColorVertexBuffer
	)
{
	check( InParentMesh != NULL ); // Perché la mesh parent non può essere NULL?

	struct InitStaticMeshVertexFactoryParams
	{
		FLocalVertexFactory* VertexFactory;
		const FStaticMeshLODResources* LODResources;
		bool bOverrideColorVertexBuffer;
		uint32 LightMapCoordinateIndex;
		uint32 LODIndex;
	} Params;

	uint32 LightMapCoordinateIndex = (uint32)InParentMesh->LightMapCoordinateIndex;
	LightMapCoordinateIndex = LightMapCoordinateIndex < LodResources.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() ? LightMapCoordinateIndex : LodResources.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() - 1;

	Params.VertexFactory = &InOutVertexFactory;
	Params.LODResources = &LodResources;
	Params.bOverrideColorVertexBuffer = bInOverrideColorVertexBuffer;
	Params.LightMapCoordinateIndex = LightMapCoordinateIndex;
	Params.LODIndex = LODIndex;

	// Initialize the static mesh's vertex factory.
	ENQUEUE_RENDER_COMMAND(InitStaticMeshVertexFactory)(
		[Params](FRHICommandListImmediate& RHICmdList)
		{
			FLocalVertexFactory::FDataType Data;

            // Inizializza il membro PositionComponent di Data
            /*
            StaticMeshData.PositionComponent = FVertexStreamComponent(
                this,
                STRUCT_OFFSET(FPositionVertex, Position),
                GetStride(),
                VET_Float3
            );
            StaticMeshData.PositionComponentSRV = PositionComponentSRV;
            */
            // Il procedimento è lo stesso per gli altri buffer, cambia il quarto parametro di FVertexStreamComponent
			Params.LODResources->VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(Params.VertexFactory, Data);

            // FA della roba magica in base a se negli shader si usano degli half
            // o dei float
            // configura Data.TangentBasisComponent[0] e Data.TangentBasisComponent[1] ( Questo codice lo copio ed incollo, ez)
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(Params.VertexFactory, Data);

            // Da copiare ed incollare pure questo
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(Params.VertexFactory, Data);

            // Same
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(Params.VertexFactory, Data, Params.LightMapCoordinateIndex);

			// bOverrideColorVertexBuffer means we intend to override the color later.  We must construct the vertexfactory such that it believes a proper stride (not 0) is set for
			// the color stream so that the real stream works later.
			if(Params.bOverrideColorVertexBuffer)
			{ 
				FColorVertexBuffer::BindDefaultColorVertexBuffer(Params.VertexFactory, Data, FColorVertexBuffer::NullBindStride::FColorSizeForComponentOverride);
			}
			//otherwise just bind the incoming buffer directly.
			else
			{
				Params.LODResources->VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(Params.VertexFactory, Data);
			}

			Data.LODLightmapDataIndex = Params.LODIndex;
			Params.VertexFactory->SetData(Data);
			Params.VertexFactory->InitResource();
		});
}
```

Essenzialmente:
1) Si configurano Data.PositionComponent e Data.PositionComponentSRV
2) Si configurano Data.TangentBasisComponent[0], Data.TangentBasisComponent[1] e Data.TangentsSRV;
3) Si configurano Data.TextureCoordinates e Data.TextureCoordinatesSRV;
4) Si configurano Data.PositionComponent e Data.PositionComponentSRV
5) Si configurano Data.LightMapCoordinateComponent e Data.TextureCoordinatesSRV
6) Si configura il color buffer in modo da avere abbastanza spazio in caso si decida di personalizzare il colore
7) Si fa SetData e si inizializza la VertexFactory
A posto, è stata creata la VertexFactory, si possono creare le FMeshBatch (seguendo l'approccio di UStaticMesh)

### Riassunto generico per la generazione della Vertex Factory
1) Si creano l'index buffer, il vertex (position) buffer, il tangents buffer, il texcoords buffer (Eventualmente il color buffer, altrimenti si usa quello di default)
2) Si creano gli SRV per il position buffer, il tangents buffer, il texcoords buffer
3) Si crea la Vertex Factory

### Analisi di FStaticMeshSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
Nello specifico, si parla del drawing di un mesh element (Mesh section) ad uno specifico LOD
Da ricordare: PDI->DrawMesh(MeshBatch, FLT_MAX);

Le `FMeshBatch` vengono generate tramite `GetMeshElement` e poi "rifinite" con `SetupMeshBatchForRuntimeVirtualTexture`
* `GetMeshElement` (per semplicità si assume che non si fa l'override dei colori)
```c++
/** Sets up a FMeshBatch for a specific LOD and element. */
bool FStaticMeshSceneProxy::GetMeshElement(
	int32 LODIndex, 
	int32 BatchIndex, 
	int32 SectionIndex, 
	uint8 InDepthPriorityGroup, 
	bool bUseSelectionOutline,
	bool bAllowPreCulledIndices, 
	FMeshBatch& OutMeshBatch) const
{
    // Si ottiene il feature level, la mesh al LOD con le corrispettive sections e vertex factory
	const ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
	const FStaticMeshLODResources& LOD = RenderData->LODResources[LODIndex];
	const FStaticMeshVertexFactories& VFs = RenderData->LODVertexFactories[LODIndex];
	const FStaticMeshSection& Section = LOD.Sections[SectionIndex];
	const FLODInfo& ProxyLODInfo = LODs[LODIndex];

    // Si ottiene il materiale e il rispettivo Proxy
	UMaterialInterface* MaterialInterface = ProxyLODInfo.Sections[SectionIndex].Material;
	FMaterialRenderProxy* MaterialRenderProxy = MaterialInterface->GetRenderProxy();
	const FMaterial* Material = MaterialRenderProxy->GetMaterial(FeatureLevel);

	const FVertexFactory* VertexFactory = nullptr;

    // Si usa in genere solo il primo Element
	FMeshBatchElement& OutMeshBatchElement = OutMeshBatch.Elements[0];
    // Has the mesh component overridden the vertex color stream for this mesh LOD?
	if (ProxyLODInfo.OverrideColorVertexBuffer)
	{ }
	else
	{
		VertexFactory = &VFs.VertexFactory;
        // TODO: Come viene interpretato?
		OutMeshBatchElement.VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer();
	}

	const bool bWireframe = false;

	// Disable adjacency information when the selection outline is enabled, since tessellation won't be used.
	const bool bRequiresAdjacencyInformation = !bUseSelectionOutline && RequiresAdjacencyInformation(MaterialInterface, VertexFactory->GetType(), FeatureLevel);

	// Two sided material use bIsFrontFace which is wrong with Reversed Indices. AdjacencyInformation use another index buffer.
	const bool bUseReversedIndices = GUseReversedIndexBuffer && IsLocalToWorldDeterminantNegative() && (LOD.bHasReversedIndices != 0) && !bRequiresAdjacencyInformation && !Material->IsTwoSided();

	// No support for stateless dithered LOD transitions for movable meshes
	const bool bDitheredLODTransition = !IsMovable() && Material->IsDitheredLODTransition();

	const uint32 NumPrimitives = SetMeshElementGeometrySource(LODIndex, SectionIndex, bWireframe, bRequiresAdjacencyInformation, bUseReversedIndices, bAllowPreCulledIndices, VertexFactory, OutMeshBatch);

	if(NumPrimitives > 0)
	{
        // Ah quindi le FMeshBatch supportano la segmentazione di default?
        // Lo setto a 0 hehe
		OutMeshBatch.SegmentIndex = SectionIndex;

        // Same
        // Btw questo credo di poterlo copiare ed incollare direttamente
		OutMeshBatch.LODIndex = LODIndex;
		OutMeshBatch.ReverseCulling = IsReversedCullingNeeded(bUseReversedIndices);
		OutMeshBatch.CastShadow = bCastShadow && Section.bCastShadow;
		OutMeshBatch.DepthPriorityGroup = (ESceneDepthPriorityGroup)InDepthPriorityGroup;
        // Informazioni sull'iluminazione, la documentazione dice che può essere NULL, quindi io lo setterò a null hehe
		OutMeshBatch.LCI = &ProxyLODInfo;
        // Obbligatorio
		OutMeshBatch.MaterialRenderProxy = MaterialRenderProxy;

		OutMeshBatchElement.MinVertexIndex = Section.MinVertexIndex;
		OutMeshBatchElement.MaxVertexIndex = Section.MaxVertexIndex;
		SetMeshElementScreenSize(LODIndex, bDitheredLODTransition, OutMeshBatch);

// C'è il lod
		return true;
	}
	else
	{
		return false;
	}
```

Riassunto di SetMeshElementGeometrySource
```c++
uint32 FStaticMeshSceneProxy::SetMeshElementGeometrySource(
	int32 LODIndex,
	int32 SectionIndex,
	bool bWireframe,
	bool bRequiresAdjacencyInformation,
	bool bUseReversedIndices,
	bool bAllowPreCulledIndices,
	const FVertexFactory* VertexFactory,
	FMeshBatch& OutMeshBatch) const
{
	const FStaticMeshLODResources& LODModel = RenderData->LODResources[LODIndex];
	const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
	const FLODInfo& LODInfo = LODs[LODIndex];
	const FLODInfo::FSectionInfo& SectionInfo = LODInfo.Sections[SectionIndex];

	FMeshBatchElement& OutMeshBatchElement = OutMeshBatch.Elements[0];
	uint32 NumPrimitives = 0;

	const bool bHasPreculledTriangles = ...
	const bool bUsePreculledIndices = ...

	if (bWireframe)
	{
		// Assumo che no wireframe
	}
	else
	{
        // EPrimitiveType
		OutMeshBatch.Type = PT_TriangleList;

		if (bUsePreculledIndices)
		{
            // Assumo di no
		}
		else
		{
			OutMeshBatchElement.IndexBuffer = bUseReversedIndices ? &LODModel.AdditionalIndexBuffers->ReversedIndexBuffer : &LODModel.IndexBuffer;
			OutMeshBatchElement.FirstIndex = Section.FirstIndex;
			NumPrimitives = Section.NumTriangles;
		}
	}

	if (bRequiresAdjacencyInformation)
	{
        // Assumo di no
	}

    // Qui si setta la Vertex Factory e il numero di primitive da disegnare
	OutMeshBatchElement.NumPrimitives = NumPrimitives;
	OutMeshBatch.VertexFactory = VertexFactory;

	return NumPrimitives;
}
```
SetupMeshBatchForRuntimeVirtualTexture(FMeshBatch& MeshBatch)
```c++

inline void SetupMeshBatchForRuntimeVirtualTexture(FMeshBatch& MeshBatch)
{
	MeshBatch.CastShadow = 0;
	MeshBatch.bUseAsOccluder = 0;
	MeshBatch.bUseForDepthPass = 0;
	MeshBatch.bUseForMaterial = 0;
	MeshBatch.bDitheredLODTransition = 0;
	MeshBatch.bRenderToVirtualTexture = 1;
}
```

Quindi al caso semplice questo codice dovrebbe bastare?
 
```c++
void FStaticMeshSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) {
    
    // Determine the DPG the primitive should be drawn in.
    uint8 PrimitiveDPG = GetStaticDepthPriorityGroup();
    int32 NumLODs = RenderData->LODResources.Num();
    //Never use the dynamic path in this path, because only unselected elements will use DrawStaticElements
    bool bIsMeshElementSelected = false;
	const ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
    
    FMeshBatch OutMeshBatch;
    // Si usa in genere solo il primo Element
	FMeshBatchElement& OutMeshBatchElement = OutMeshBatch.Elements[0];

    // Si ottiene il materiale e il rispettivo Proxy
	UMaterialInterface* MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
	FMaterialRenderProxy* MaterialRenderProxy = MaterialInterface->GetRenderProxy();
	const FVertexFactory* VertexFactory = VertexFactoryConfigurataInPrecedenza;


    // EPrimitiveType
    OutMeshBatch.Type = PT_TriangleList;
    OutMeshBatchElement.IndexBuffer = IndexBufferCreatoPrima
    OutMeshBatchElement.FirstIndex = 0;
    NumPrimitives = Section.NumTriangles;

    // Qui si setta la Vertex Factory e il numero di primitive da disegnare
	OutMeshBatchElement.NumPrimitives = NumPrimitives;
	OutMeshBatch.VertexFactory = VertexFactory;

    OutMeshBatch.SegmentIndex = 0;
    OutMeshBatch.LODIndex = 0;
    OutMeshBatch.ReverseCulling = IsReversedCullingNeeded(bUseReversedIndices);
    OutMeshBatch.LCI = nullptr;
    // Obbligatorio
    OutMeshBatch.MaterialRenderProxy = MaterialRenderProxy;

    OutMeshBatchElement.MinVertexIndex = 0; // 0 in genere
    OutMeshBatchElement.MaxVertexIndex = NumVertici - 1;
	OutMeshBatch.CastShadow = 0;
	OutMeshBatch.bUseAsOccluder = 0;
	OutMeshBatch.bUseForDepthPass = 0;
	OutMeshBatch.bUseForMaterial = 0;
	OutMeshBatch.bDitheredLODTransition = 0;
	OutMeshBatch.bRenderToVirtualTexture = 1;
    PDI->DrawMesh(OutMeshBatch, FLT_MAX);
}
```

Per il rendering con il path dinamico si usa GetDynamicMeshElements (esempio preso da FUnrealNexusProxy).
```
void FUnrealNexusProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
    const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
    {

        const auto& EngineShowFlags = ViewFamily.EngineShowFlags;
	
        if (!(VisibilityMap & (1 << ViewIndex))) continue;
        for (const auto& NodeIndexAndRenderingData : LoadedMeshData) 
        {
            FNexusNodeRenderData* Data = NodeIndexAndRenderingData.Value;
            FMeshBatch& Mesh = Collector.AllocateMesh();
            Mesh.bWireframe = EngineShowFlags.Wireframe;
            Mesh.VertexFactory = &Data->NodeVertexFactory;
            Mesh.Type = PT_TriangleList;
            Mesh.DepthPriorityGroup = SDPG_World;
            Mesh.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
    
            auto& Element = Mesh.Elements[0];
            Element.IndexBuffer = &Data->IndexBuffer;
            Element.FirstIndex = 0;
            Element.NumPrimitives = Data->NumPrimitives;
            Collector.AddMesh(ViewIndex, Mesh);
        }
    }
}
```
RHICreateShaderResourceView(FShaderResourceViewInitializer(VertexBufferRHI, PF_R32_FLOAT));
```
```
Nota (da confermare): la batch deve vivere almeno quanto l'FMeshCollector, un modo per garantire ciò è allocare la MeshBatch usando FMeshElementCollector::AllocateMesh().
Infine si usa FMeshElementCollector::AddMesh() per disegnare la mesh 