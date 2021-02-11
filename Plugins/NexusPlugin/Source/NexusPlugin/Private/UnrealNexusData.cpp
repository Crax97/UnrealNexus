#include "UnrealNexusData.h"
#include "NexusUtils.h"

#include "corto/decoder.h"
#include "space/intersection3.h"
#include "space/line3.h"

DEFINE_LOG_CATEGORY(NexusInfo);
DEFINE_LOG_CATEGORY(NexusErrors);

using namespace nx;

class FDNode {
public:
    uint32_t Node;
    float Dist;
    explicit FDNode(const uint32_t N = 0, const float D = 0.0f): Node(N), Dist(D) {}
    bool operator<(const FDNode &N) const {
        return Dist < N.Dist;
    }
};
void LogHeaderInfo(const Header& Header)
{
    UE_LOG(NexusInfo, Log, TEXT("NEXUS: Read file with header version %d"), Header.version);
    UE_LOG(NexusInfo, Log, TEXT("\t %d vertices"), Header.nvert);
    UE_LOG(NexusInfo, Log, TEXT("\t %d faces"), Header.nface);
    UE_LOG(NexusInfo, Log, TEXT("\t %d nodes"), Header.n_nodes);
    UE_LOG(NexusInfo, Log, TEXT("\t %d patches"), Header.n_patches);
    UE_LOG(NexusInfo, Log, TEXT("\t %d textures"), Header.n_textures);
    UE_LOG(NexusInfo, Log, TEXT("\t sphere: center (%f, %f, %f) radius %f"), Header.sphere.Center().X(),Header.sphere.Center().Y(),Header.sphere.Center().Z(), Header.sphere.Radius());
}


char* ToRGBA8888(char* RawData, const nx::TextureData& Data)
{
    // TODO: Transform from RGB to RGBA
    return RawData;
}

void LoadImageRawData(UTexture2D* Image, TextureData& Data)
{
    char* DataPtr = static_cast<char*>(Image->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memcpy(DataPtr, Data.memory, Data.height * Data.width * 4);
    Image->PlatformData->Mips[0].BulkData.Unlock();
    Image->PlatformData->SetNumSlices(1);
    Image->NeverStream = true;
    Image->UpdateResource();
}

UUnrealNexusData::~UUnrealNexusData()
{
    Nodes = new Node[Header.n_nodes];
    Patches = new Patch[Header.n_patches];
    Textures = new Texture[Header.n_textures];
    NodeData = new nx::NodeData[Header.n_nodes];
    TextureData = new nx::TextureData[Header.n_textures];

}

vcg::Sphere3f& UUnrealNexusData::BoundingSphere()
{
    return Header.sphere;
}

bool Closest(vcg::Sphere3f &Sphere, vcg::Ray3f &Ray, float &Distance) {
    vcg::Point3f Dir = Ray.Direction();
    const vcg::Line3f Line(Ray.Origin(), Dir.Normalize());

    vcg::Point3f p, q;
    const bool Success = vcg::IntersectionLineSphere(Sphere, Line, p, q);
    if(!Success) return false;
    p -= Ray.Origin();
    q -= Ray.Origin();
    float a = p*Ray.Direction();
    const float b = q*Ray.Direction();
    if(a > b) a = b;
    if(b < 0) return false; //behind observer
    if(a < 0) {   //we are inside
        Distance = 0;
        return true;
    }
    Distance = a;
    return true;
}

auto UUnrealNexusData::Intersects(vcg::Ray3f& Ray, float& Distance) -> bool
{
	const uint32_t Sink = Header.n_nodes -1;

    vcg::Point3f Dir = Ray.Direction();
    Ray.SetDirection(Dir.Normalize());

    if(!Closest(Header.sphere, Ray, Distance))
    	return false;

    Distance = 1e20f;

    //keep track of visited nodes (it is a DAG not a tree!
    std::vector<bool> Visited(Header.n_nodes, false);
    std::vector<FDNode> Candidates;      //heap of nodes
    Candidates.push_back(FDNode(0, Distance));

    bool Hit = false;

    while(Candidates.size()) {
    	pop_heap(Candidates.begin(), Candidates.end());
        const FDNode Candidate = Candidates.back();
    	Candidates.pop_back();

    	if(Candidate.Dist > Distance) break;

    	Node &Node = Nodes[Candidate.Node];

    	if(Node.first_patch  == Sink) {
    		if(Candidate.Dist < Distance) {
    			Distance = Candidate.Dist;
    			Hit = true;
    		}
    		continue;
    	}

    	//not a leaf, insert children (if they intersect
    	for(uint32_t i = Node.first_patch; i < Node.last_patch(); i++) {
            const uint32_t Child = Patches[i].node;
    		if(Child == Sink) continue;
    		if(Visited[Child]) continue;

    		float d;
    		if(Closest(Nodes[Child].sphere, Ray, d)) {
    			Candidates.push_back(FDNode(Child, d));
    			push_heap(Candidates.begin(), Candidates.end());
    			Visited[Child] = true;
    		}
    	}
    }
    Distance = FMath::Sqrt(Distance);
    return Hit;
}

uint32_t UUnrealNexusData::Size(const uint32_t Node) const
{
    return Nodes[Node].getSize();
}

UUnrealNexusData::UUnrealNexusData() {}
