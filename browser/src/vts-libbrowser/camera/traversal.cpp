/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "../camera.hpp"
#include "../traverseNode.hpp"
#include "../gpuResource.hpp"
#include "../renderTasks.hpp"
#include "../geodata.hpp"
#include "../coordsManip.hpp"
#include "../metaTile.hpp"
#include "../mapLayer.hpp"
#include "../mapConfig.hpp"
#include "../map.hpp"

namespace vts
{

double CameraImpl::travDistance(TraverseNode *trav, const vec3 pointPhys)
{
    // checking the distance in node srs may be more accurate,
    //   but the resulting distance is in different units
    return aabbPointDist(pointPhys, trav->meta->aabbPhys[0], trav->meta->aabbPhys[1]);
}

void CameraImpl::updateNodePriority(TraverseNode *trav)
{
    if (trav->meta)
        trav->priority = (float)(1e6 / (travDistance(trav, focusPosPhys) + 1));
    else if (trav->parent)
        trav->priority = trav->parent->priority;
    else
        trav->priority = 0;
}

std::shared_ptr<GpuTexture> CameraImpl::travInternalTexture(TraverseNode *trav, uint32 subMeshIndex)
{
    UrlTemplate::Vars vars(trav->id, trav->meta->localId, subMeshIndex);
    std::shared_ptr<GpuTexture> res = map->getTexture(trav->surface->urlIntTex(vars));
    map->touchResource(res);
    res->updatePriority(trav->priority);
    return res;
}

bool CameraImpl::generateMonolithicGeodataTrav(TraverseNode *trav)
{
    assert(!!trav->layer->freeLayer);
    assert(!!trav->layer->freeLayerParams);
    const vtslibs::registry::FreeLayer::Geodata &g = boost::get<vtslibs::registry::FreeLayer::Geodata>(trav->layer->freeLayer->definition);
    trav->meta = std::make_shared<const MetaNode>(generateMetaNode(map->mapconfig, map->convertor, trav->id, g));
    trav->surface = &trav->layer->surfaceStack.surfaces[0];
    updateNodePriority(trav);
    return true;
}

bool CameraImpl::travDetermineMeta(TraverseNode *trav, bool initAllChild)
{
    assert(trav->layer);
    assert(!trav->meta);
    assert(trav->childs.empty());
    assert(!trav->determined);
    assert(trav->rendersEmpty());
    assert(!trav->parent || trav->parent->meta);

    // statistics
    statistics.currentNodeMetaUpdates++;

    // handle non-tiled geodata
    if (trav->layer->freeLayer && trav->layer->freeLayer->type == vtslibs::registry::FreeLayer::Type::geodata)
        return generateMonolithicGeodataTrav(trav);

    // retrieve metatile resource handles
    const TileId nodeId = trav->id;
    if (trav->metaTiles.empty())
    {
        trav->metaTiles.resize(trav->layer->surfaceStack.surfaces.size());
        const UrlTemplate::Vars tileIdVars(map->roundId(nodeId));
        for (uint32 i = 0, e = trav->metaTiles.size(); i != e; i++)
        {
            if (trav->parent)
            {
                const std::shared_ptr<MetaTile> &p = trav->parent->metaTiles[i];
                if (!p)
                    continue;
                TileId pid = vtslibs::vts::parent(nodeId);
                uint32 idx = (nodeId.x % 2) + (nodeId.y % 2) * 2;
                const vtslibs::vts::MetaNode &node = p->get(pid);
                if ((node.flags() & (vtslibs::vts::MetaNode::Flag::ulChild << idx)) == 0)
                    continue;
            }
            trav->metaTiles[i] = map->getMetaTile(trav->layer->surfaceStack.surfaces[i].urlMeta(tileIdVars));
        }
    }

    // check metatiles download status
    bool determined = true;
    for (const auto &m : trav->metaTiles)
    {
        if (!m)
            continue;
        m->updatePriority(trav->priority * 2);
        switch (map->getResourceValidity(m))
        {
        case Validity::Indeterminate:
            determined = false;
            UTILITY_FALLTHROUGH;
        case Validity::Invalid:
            continue;
        case Validity::Valid:
            break;
        }
    }
    if (!determined)
        return false;

    // find topmost nonempty surface
    const SurfaceInfo *topmost = nullptr;
    std::shared_ptr<MetaTile> chosen;
    bool childsAvailable[4] = {false, false, false, false};
    for (uint32 i = 0, e = trav->metaTiles.size(); i != e; i++)
    {
        const auto &m = trav->metaTiles[i];
        if (!m)
            continue;
        const vtslibs::vts::MetaNode &n = m->get(nodeId);
        for (uint32 j = 0; j < 4; j++)
            childsAvailable[j] = childsAvailable[j] || (n.childFlags() & (vtslibs::vts::MetaNode::Flag::ulChild << j));
        if (topmost || n.alien() != trav->layer->surfaceStack.surfaces[i].alien)
            continue;
        if (n.geometry())
        {
            chosen = m;
            if (trav->layer->tilesetStack)
            {
                assert(n.sourceReference > 0 && n.sourceReference <= trav->layer->tilesetStack->surfaces.size());
                topmost = &trav->layer->tilesetStack->surfaces[n.sourceReference];
            }
            else
                topmost = &trav->layer->surfaceStack.surfaces[i];
        }
        if (!chosen)
            chosen = m;
    }
    if (!chosen)
        return false; // all surfaces failed to download, what can i do?

    // surface
    if (topmost)
    {
        trav->surface = topmost;
        // credits
        for (auto it : chosen->get(nodeId).credits())
            trav->credits.push_back(it);
    }

    // meta node
    trav->meta = chosen->getNode(nodeId);

    // prepare children
    if (initAllChild || childsAvailable[0] || childsAvailable[1] || childsAvailable[2] || childsAvailable[3])
    {
        vtslibs::vts::Children childs = vtslibs::vts::children(nodeId);
        trav->childs.ptr = std::make_unique<TraverseChildsArray>();
        for (uint32 i = 0; i < 4; i++)
            if (initAllChild || childsAvailable[i]) {
                trav->childs.ptr->arr.emplace_back(trav->layer, trav, childs[i]);
            }
    }

    // update priority
    updateNodePriority(trav);

    return true;
}

bool CameraImpl::travDetermineDraws(TraverseNode *trav)
{
    assert(trav->meta);
    touchDraws(trav);
    if (!trav->surface || trav->determined)
        return trav->determined;
    assert(trav->rendersEmpty());

    // statistics
    statistics.currentNodeDrawsUpdates++;

    // update priority
    updateNodePriority(trav);

    if (trav->layer->isGeodata())
        return trav->determined = travDetermineDrawsGeodata(trav);
    else
        return trav->determined = travDetermineDrawsSurface(trav);
}

bool CameraImpl::travDetermineDrawsSurface(TraverseNode *trav)
{
    assert(!trav->determined);
    assert(trav->rendersEmpty());

    const TileId nodeId = trav->id;

    // wait for resources to download
    for (const auto &it : trav->resources)
        map->touchResource(it);
    for (const auto &it : trav->resources)
        if (map->getResourceValidity(it) == Validity::Indeterminate)
            return false;
    trav->resources.clear();

    // aggregate mesh
    std::shared_ptr<MeshAggregate> meshAgg;
    {
        const std::string name = trav->surface->urlMesh(UrlTemplate::Vars(nodeId, trav->meta->localId));
        meshAgg = map->getMeshAggregate(name);
        trav->resources.push_back(meshAgg);
    }
    meshAgg->updatePriority(trav->priority);
    switch (map->getResourceValidity(meshAgg))
    {
    case Validity::Invalid:
        trav->surface = nullptr;
        trav->resources.clear();
        return false;
    case Validity::Indeterminate:
        return false;
    case Validity::Valid:
        break;
    }

    // individual meshes
    bool determined = true;
    decltype(trav->opaque) newOpaque;
    decltype(trav->transparent) newTransparent;
    decltype(trav->credits) newCredits;
    for (uint32 subMeshIndex = 0, e = meshAgg->submeshes.size(); subMeshIndex != e; subMeshIndex++)
    {
        const MeshPart &part = meshAgg->submeshes[subMeshIndex];
        std::shared_ptr<GpuMesh> mesh = part.renderable;

        // external bound textures
        if (part.externalUv)
        {
            BoundParamInfo::List bls = trav->layer->boundList(trav->surface, part.surfaceReference);
            if (part.textureLayer)
                bls.push_back(BoundParamInfo(vtslibs::registry::View::BoundLayerParams(map->mapconfig->boundLayers.get(part.textureLayer).id)));
            const Validity validity = reorderBoundLayers(trav->id, trav->meta->localId, subMeshIndex, bls, trav->priority);

            for (const BoundParamInfo &it : bls)
            {
                if (it.boundMetaTile)
                    trav->resources.push_back(it.boundMetaTile);
                if (it.textureColor)
                    trav->resources.push_back(it.textureColor);
                if (it.textureMask)
                    trav->resources.push_back(it.textureMask);
            }

            switch (validity)
            {
            case Validity::Indeterminate:
                determined = false;
                continue;
            case Validity::Invalid:
                continue;
            case Validity::Valid:
                break;
            }

            bool anyOpaqueLayer = false;
            for (const BoundParamInfo &b : bls)
                if (!b.transparent && !b.textureMask)
                    anyOpaqueLayer = true;

            bool allTransparent = true;
            for (const BoundParamInfo &b : bls)
            {
                // credits
                {
                    const BoundInfo *l = b.bound;
                    assert(l);
                    for (const auto &it : l->credits)
                    {
                        auto c = map->credits->find(it.first);
                        if (c)
                            newCredits.push_back(*c);
                    }
                }

                // draw task
                RenderSurfaceTask task;
                task.textureColor = b.textureColor;
                task.textureMask = b.textureMask;
                task.color(3) = b.alpha ? *b.alpha : 1;
                task.mesh = mesh;
                task.model = part.normToPhys;
                task.uvTrans = b.uvTrans();
                task.externalUv = true;
                task.boundLayerId = b.id;

                bool renderTransparent = b.transparent;
                if (!renderTransparent && b.textureMask)
                {
                    // layers with texture mask should be rendered as transparencies, which ensures consistent ordering
                    // however, there has to be at least one opaque layer to ensure that depth buffer is written
                    if (anyOpaqueLayer)
                        renderTransparent = true;
                    else
                        anyOpaqueLayer = true;
                }

                if (renderTransparent)
                    newTransparent.push_back(task);
                else
                    newOpaque.push_back(task);

                allTransparent = allTransparent && b.transparent;
            }
            if (!allTransparent)
                continue; // skip internal texture
        }

        // internal texture
        if (part.internalUv)
        {
            RenderSurfaceTask task;
            task.textureColor = travInternalTexture(trav, subMeshIndex);
            trav->resources.push_back(task.textureColor);
            switch (map->getResourceValidity(task.textureColor))
            {
            case Validity::Indeterminate:
                determined = false;
                continue;
            case Validity::Invalid:
                continue;
            case Validity::Valid:
                break;
            }
            task.mesh = mesh;
            task.model = part.normToPhys;
            task.externalUv = false;
            newOpaque.insert(newOpaque.begin(), task);
        }
    }

    if (determined)
    {
        // renders
        std::swap(trav->opaque, newOpaque);
        std::swap(trav->transparent, newTransparent);

        // colliders
        for (uint32 subMeshIndex = 0, e = meshAgg->submeshes.size(); subMeshIndex != e; subMeshIndex++)
        {
            const MeshPart &part = meshAgg->submeshes[subMeshIndex];
            std::shared_ptr<GpuMesh> mesh = part.renderable;
            RenderColliderTask task;
            task.mesh = mesh;
            task.model = part.normToPhys;
            trav->colliders.push_back(task);
        }

        // credits
        trav->credits.insert(trav->credits.end(), newCredits.begin(), newCredits.end());

        // discard temporary
        trav->resources.shrink_to_fit();
    }

    return determined;
}

bool CameraImpl::travDetermineDrawsGeodata(TraverseNode *trav)
{
    assert(!trav->determined);
    assert(trav->rendersEmpty());
    assert(trav->resources.empty());

    const TileId nodeId = trav->id;
    const std::string geoName = trav->surface->urlGeodata(UrlTemplate::Vars(nodeId, trav->meta->localId));

    auto style = map->getActualGeoStyle(trav->layer->freeLayerName);
    auto features = map->getActualGeoFeatures(trav->layer->freeLayerName, geoName, trav->priority);
    if (style.first == Validity::Invalid || features.first == Validity::Invalid)
    {
        trav->surface = nullptr;
        return false;
    }
    if (style.first == Validity::Indeterminate || features.first == Validity::Indeterminate)
        return false;

    std::shared_ptr<GeodataTile> geo = map->getGeodata(geoName + "#tile");
    geo->updatePriority(trav->priority);
    geo->update(style.second, features.second, map->mapconfig->browserOptions.value, trav->meta->aabbPhys, trav->id);
    switch (map->getResourceValidity(geo))
    {
    case Validity::Invalid:
        trav->surface = nullptr;
        return false;
    case Validity::Indeterminate:
        return false;
    case Validity::Valid:
        break;
    }

    // determined
    assert(!trav->determined);
    assert(trav->rendersEmpty());

    // copy draws
    for (const ResourceInfo &r : geo->renders)
    {
        DrawGeodataTask t;
        t.geodata = std::shared_ptr<void>(geo, r.userData.get());
        trav->geodata.emplace_back(t);
    }

    return true;
}

bool CameraImpl::travInit(TraverseNode *trav, bool initAllChildren)
{
    // statistics
    {
        statistics.metaNodesTraversedTotal++;
        statistics.metaNodesTraversedPerLod[std::min<uint32>(trav->id.lod, CameraStatistics::MaxLods-1)]++;
    }

    // update trav
    trav->lastAccessTime = map->renderTickIndex;
    updateNodePriority(trav);

    // prepare meta data
    if (!trav->meta)
    {
        for (const auto &it : trav->metaTiles)
        {
            if (it)
                map->touchResource(it);
        }
        return travDetermineMeta(trav, initAllChildren);
    }

    return true;
}

void CameraImpl::travModeHierarchical(TraverseNode *trav, bool loadOnly)
{
    if (!travInit(trav))
        return;

    // the resources may not be unloaded
    trav->lastRenderTime = trav->lastAccessTime;

    travDetermineDraws(trav);

    if (loadOnly)
        return;

    if (!visibilityTest(trav))
        return;

    if (coarsenessTest(trav) || trav->childs.empty())
    {
        if (trav->determined)
            renderNode(trav);
        return;
    }

    bool ok = true;
    for (auto &t : trav->childs)
    {
        if (!t.meta)
        {
            ok = false;
            continue;
        }
        if (t.surface && !t.determined)
            ok = false;
    }

    for (auto &t : trav->childs)
        travModeHierarchical(&t, !ok);

    if (!ok && trav->determined)
        renderNode(trav);
}

void CameraImpl::travModeFlat(TraverseNode *trav)
{
    if (!travInit(trav))
        return;

    if (!visibilityTest(trav))
        return;

    if (coarsenessTest(trav) || trav->childs.empty())
    {
        if (travDetermineDraws(trav))
            renderNode(trav);
        return;
    }

    for (auto &t : trav->childs)
        travModeFlat(&t);
}

// mode == 0 -> default
// mode == 1 -> load only -> returns true if loaded
// mode == 2 -> render only
bool CameraImpl::travModeStable(TraverseNode *trav, int mode)
{
    if (mode == 2)
    {
        if (!trav->meta)
            return false;
        trav->lastAccessTime = map->renderTickIndex;
    }
    else
    {
        if (!travInit(trav))
            return false;
    }

    if (!visibilityTest(trav))
        return true;

    if (mode == 2)
    {
        if (trav->determined)
        {
            touchDraws(trav);
            renderNode(trav);
        }
        else for (auto &t : trav->childs)
            travModeStable(&t, 2);
        return true;
    }

    if (coarsenessTest(trav) || trav->childs.empty())
    {
        travDetermineDraws(trav);
        if (mode == 1)
        {
            trav->lastRenderTime = map->renderTickIndex;
            return trav->determined;
        }
        if (trav->determined)
            renderNode(trav);
        else for (auto &t : trav->childs)
            travModeStable(&t, 2);
        return true;
    }

    if (mode == 0 && trav->determined)
    {
        bool ok = true;
        for (auto &t : trav->childs)
            ok = travModeStable(&t, 1) && ok;
        if (!ok)
        {
            touchDraws(trav);
            renderNode(trav);
            return true;
        }
    }

    {
        bool ok = true;
        for (auto &t : trav->childs)
            ok = travModeStable(&t, mode) && ok;
        return ok;
    }
}

bool myVisibilityTest(TraverseNode *trav, int lod, int a1, int b1, int a2, int b2) {
    int shift = lod - trav->id.lod;
    if (shift < 0) {
        return false;
    }
    a1 >>= shift;
    a2 >>= shift;
    b1 >>= shift;
    b2 >>= shift;
    auto ret = trav->id.x >= a1 && trav->id.x <= a2
        && trav->id.y >= b1 && trav->id.y <= b2;
    return ret;
}

bool CameraImpl::travLod(TraverseNode *trav, int lod, int a1, int b1, int a2, int b2) {
    if (!travInit(trav))
        return false;

    if (!myVisibilityTest(trav, lod, a1, b1, a2, b2))
        return true;

    if (trav->id.lod >= lod || trav->childs.empty())
        //if (coarsenessTest(trav) || trav->childs.empty())
    {
        gridPreloadRequest(trav);
        if (travDetermineDraws(trav))
        {
            renderNode(trav);
            return true;
        }
    }

    Array<bool, 4> oks;
    oks.resize(trav->childs.size());
    uint32 i = 0, okc = 0;
    for (auto &it : trav->childs)
    {
        bool ok = travLod(&it, lod, a1, b1, a2, b2);
        oks[i++] = ok;
        if (ok)
            okc++;
    }
    i = 0;
    for (auto &it : trav->childs)
    {
        if (!oks[i++])
            renderNodeCoarser(&it);
    }
    return true;
}

//bool CameraImpl::travLod(TraverseNode *trav, int lod, int a1, int b1, int a2, int b2) {
//    if (myFixedTravs.empty()) {
//        for (int a = a1; a <= a2; a++) {
//            for (int b = b1; b <= b2; b++) {
//                vtslibs::vts::TileId tileId(lod, a, b);
//                myFixedTravs.push_back(std::make_shared<TraverseNode>(trav->layer, nullptr, tileId));
//            }
//        }
//    }
//    for (auto &it : myFixedTravs) {
//        if (!travInit(it.get(), true))
//            continue;
//        gridPreloadRequest(it.get());
//        if (travDetermineDraws(it.get()))
//        {
//            renderNode(it.get());
//        }
//    }
//    return true;
//};

bool CameraImpl::travModeBalanced(TraverseNode *trav, bool renderOnly)
{
    if (renderOnly)
    {
        if (!trav->meta)
            return false;
        trav->lastAccessTime = map->renderTickIndex;
    }
    else
    {
        if (!travInit(trav))
            return false;
    }

    if (!visibilityTest(trav))
        return true;

    if (renderOnly)
    {
        if (trav->determined)
        {
            touchDraws(trav);
            renderNode(trav);
            return true;
        }
    }
    else if (coarsenessTest(trav) || trav->childs.empty())
    {
        gridPreloadRequest(trav);
        if (travDetermineDraws(trav))
        {
            renderNode(trav);
            return true;
        }
        renderOnly = true;
    }

    Array<bool, 4> oks;
    oks.resize(trav->childs.size());
    uint32 i = 0, okc = 0;
    for (auto &it : trav->childs)
    {
        bool ok = travModeBalanced(&it, renderOnly);
        oks[i++] = ok;
        if (ok)
            okc++;
    }
    if (okc == 0 && renderOnly)
        return false;
    i = 0;
    for (auto &it : trav->childs)
    {
        if (!oks[i++])
            renderNodeCoarser(&it);
    }
    return true;
}

void CameraImpl::travModeFixed(TraverseNode *trav)
{
    if (!travInit(trav))
        return;

    if (travDistance(trav, focusPosPhys) > options.fixedTraversalDistance)
        return;

    if (trav->id.lod >= options.fixedTraversalLod || trav->childs.empty())
    {
        if (travDetermineDraws(trav))
            renderNode(trav);
        return;
    }

    for (auto &t : trav->childs)
        travModeFixed(&t);
}

const int max_lod_diff = 4;

// return rendered or not
bool CameraImpl::travModeDistanceBaseFixed(TraverseNode *trav)
{
    if (!travInit(trav))
        return false;

    int lodDiff = std::max(0, int(options.fixedTraversalLod - trav->id.lod));
    double targetTraversalDistance = options.fixedTraversalDistance * pow(2, lodDiff);

    auto tileDistance = travDistance(trav, focusPosPhys);
    if (tileDistance > targetTraversalDistance)
        return false;

    if ((lodDiff < max_lod_diff && tileDistance > targetTraversalDistance / 2) || trav->childs.empty())
    {
//        printf("renderNode: %2d - %d %d\n", trav->id.lod, trav->id.x, trav->id.y);
        if (travDetermineDraws(trav))
            renderNode(trav);
        return true;
    }

    bool isRendered = false;

    int childrenCount = trav->childs.size();
    std::vector<bool> rendered(childrenCount);
    {
        int i = 0;
        for (auto& t : trav->childs) {
            rendered[i] = travModeDistanceBaseFixed(&t);
            if (rendered[i]) {
                isRendered = true;
            }
            i++;
        }
    }

    if (lodDiff > max_lod_diff) {
        return isRendered;
    }

    if (!isRendered) {
        return isRendered;
    }

    {
        int i = 0;
        for (auto& t : trav->childs) {
            if (rendered[i++]) {
                continue;
            }
            if (travDetermineDraws(&t))
                renderNode(&t);
        }
    }

    return isRendered;
}


void CameraImpl::traverseRender(TraverseNode *trav)
{
    switch (trav->layer->isGeodata() ? options.traverseModeGeodata : options.traverseModeSurfaces)
    {
    case TraverseMode::None:
        break;
    case TraverseMode::Flat:
        travModeFlat(trav);
        break;
    case TraverseMode::Stable:
        travModeStable(trav, 0);
        break;
    case TraverseMode::Balanced:
        travModeBalanced(trav, false);
        break;
    case TraverseMode::Hierarchical:
        travModeHierarchical(trav, false);
        break;
    case TraverseMode::Fixed:
        travModeFixed(trav);
        break;
    case TraverseMode::DistanceBaseFixed:
        travModeDistanceBaseFixed(trav);
        break;
    default:
        assert(false);
    }
}

} // namespace vts
