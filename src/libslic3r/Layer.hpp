#ifndef slic3r_Layer_hpp_
#define slic3r_Layer_hpp_

#include "libslic3r.h"
#include "Flow.hpp"
#include "SurfaceCollection.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExPolygonCollection.hpp"

namespace Slic3r {

class Layer;
using LayerPtrs = std::vector<Layer*>;
class LayerRegion;
using LayerRegionPtrs = std::vector<LayerRegion*>;
class PrintRegion;
class PrintObject;

namespace FillAdaptive {
    struct Octree;
};

class LayerRegion
{
public:
    Layer*                      layer()         { return m_layer; }
    const Layer*                layer() const   { return m_layer; }
    const PrintRegion&          region() const  { return *m_region; }

    // collection of surfaces generated by slicing the original geometry
    // divided by type top/bottom/internal
    SurfaceCollection           slices;
    // Backed up slices before they are split into top/bottom/internal.
    // Only backed up for multi-region layers or layers with elephant foot compensation.
    //FIXME Review whether not to simplify the code by keeping the raw_slices all the time.
    ExPolygons                  raw_slices;

    // collection of extrusion paths/loops filling gaps
    // These fills are generated by the perimeter generator.
    // They are not printed on their own, but they are copied to this->fills during infill generation.
    ExtrusionEntityCollection   thin_fills;

    // Unspecified fill polygons, used for overhang detection ("ensure vertical wall thickness feature")
    // and for re-starting of infills.
    ExPolygons                  fill_expolygons;
    // collection of surfaces for infill generation
    SurfaceCollection           fill_surfaces;

    // collection of expolygons representing the bridged areas (thus not
    // needing support material)
//    Polygons                    bridged;

    // collection of polylines representing the unsupported bridge edges
    Polylines          			unsupported_bridge_edges;

    // ordered collection of extrusion paths/loops to build all perimeters
    // (this collection contains only ExtrusionEntityCollection objects)
    ExtrusionEntityCollection   perimeters;

    // ordered collection of extrusion paths to fill surfaces
    // (this collection contains only ExtrusionEntityCollection objects)
    ExtrusionEntityCollection   fills;
    
    Flow    flow(FlowRole role) const;
    Flow    flow(FlowRole role, double layer_height) const;
    Flow    bridging_flow(FlowRole role) const;

    void    slices_to_fill_surfaces_clipped();
    void    prepare_fill_surfaces();
    void    make_perimeters(const SurfaceCollection &slices, SurfaceCollection* fill_surfaces);
    void    process_external_surfaces(const Layer *lower_layer, const Polygons *lower_layer_covered);
    double  infill_area_threshold() const;
    // Trim surfaces by trimming polygons. Used by the elephant foot compensation at the 1st layer.
    void    trim_surfaces(const Polygons &trimming_polygons);
    // Single elephant foot compensation step, used by the elephant foor compensation at the 1st layer.
    // Trim surfaces by trimming polygons (shrunk by an elephant foot compensation step), but don't shrink narrow parts so much that no perimeter would fit.
    void    elephant_foot_compensation_step(const float elephant_foot_compensation_perimeter_step, const Polygons &trimming_polygons);

    void    export_region_slices_to_svg(const char *path) const;
    void    export_region_fill_surfaces_to_svg(const char *path) const;
    // Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
    void    export_region_slices_to_svg_debug(const char *name) const;
    void    export_region_fill_surfaces_to_svg_debug(const char *name) const;

    // Is there any valid extrusion assigned to this LayerRegion?
    bool    has_extrusions() const { return ! this->perimeters.entities.empty() || ! this->fills.entities.empty(); }

protected:
    friend class Layer;
    friend class PrintObject;

    LayerRegion(Layer *layer, const PrintRegion *region) : m_layer(layer), m_region(region) {}
    ~LayerRegion() {}

private:
    Layer             *m_layer;
    const PrintRegion *m_region;
};

class Layer 
{
public:
    // Sequential index of this layer in PrintObject::m_layers, offsetted by the number of raft layers.
    size_t              id() const          { return m_id; }
    void                set_id(size_t id)   { m_id = id; }
    PrintObject*        object()            { return m_object; }
    const PrintObject*  object() const      { return m_object; }

    Layer              *upper_layer;
    Layer              *lower_layer;
    bool                slicing_errors;
    coordf_t            slice_z;       // Z used for slicing in unscaled coordinates
    coordf_t            print_z;       // Z used for printing in unscaled coordinates
    coordf_t            height;        // layer height in unscaled coordinates
    coordf_t            bottom_z() const { return this->print_z - this->height; }

    // Collection of expolygons generated by slicing the possibly multiple meshes of the source geometry 
    // (with possibly differing extruder ID and slicing parameters) and merged.
    // For the first layer, if the Elephant foot compensation is applied, this lslice is uncompensated, therefore
    // it includes the Elephant foot effect, thus it corresponds to the shape of the printed 1st layer.
    // These lslices aka islands are chained by the shortest traverse distance and this traversal
    // order will be applied by the G-code generator to the extrusions fitting into these lslices.
    // These lslices are also used to detect overhangs and overlaps between successive layers, therefore it is important
    // that the 1st lslice is not compensated by the Elephant foot compensation algorithm.
    ExPolygons 				 lslices;
    std::vector<BoundingBox> lslices_bboxes;

    size_t                  region_count() const { return m_regions.size(); }
    const LayerRegion*      get_region(int idx) const { return m_regions[idx]; }
    LayerRegion*            get_region(int idx) { return m_regions[idx]; }
    LayerRegion*            add_region(const PrintRegion *print_region);
    const LayerRegionPtrs&  regions() const { return m_regions; }
    // Test whether whether there are any slices assigned to this layer.
    bool                    empty() const;    
    void                    make_slices();
    // Backup and restore raw sliced regions if needed.
    //FIXME Review whether not to simplify the code by keeping the raw_slices all the time.
    void                    backup_untyped_slices();
    void                    restore_untyped_slices();
    // Slices merged into islands, to be used by the elephant foot compensation to trim the individual surfaces with the shrunk merged slices.
    ExPolygons              merged(float offset) const;
    template <class T> bool any_internal_region_slice_contains(const T &item) const {
        for (const LayerRegion *layerm : m_regions) if (layerm->slices.any_internal_contains(item)) return true;
        return false;
    }
    template <class T> bool any_bottom_region_slice_contains(const T &item) const {
        for (const LayerRegion *layerm : m_regions) if (layerm->slices.any_bottom_contains(item)) return true;
        return false;
    }
    void                    make_perimeters();
    // Phony version of make_fills() without parameters for Perl integration only.
    void                    make_fills() { this->make_fills(nullptr, nullptr); }
    void                    make_fills(FillAdaptive::Octree* adaptive_fill_octree, FillAdaptive::Octree* support_fill_octree);
    void 					make_ironing();

    void                    export_region_slices_to_svg(const char *path) const;
    void                    export_region_fill_surfaces_to_svg(const char *path) const;
    // Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
    void                    export_region_slices_to_svg_debug(const char *name) const;
    void                    export_region_fill_surfaces_to_svg_debug(const char *name) const;

    // Is there any valid extrusion assigned to this LayerRegion?
    virtual bool            has_extrusions() const { for (auto layerm : m_regions) if (layerm->has_extrusions()) return true; return false; }

protected:
    friend class PrintObject;
    friend std::vector<Layer*> new_layers(PrintObject*, const std::vector<coordf_t>&);
    friend std::string fix_slicing_errors(LayerPtrs&, const std::function<void()>&);

    Layer(size_t id, PrintObject *object, coordf_t height, coordf_t print_z, coordf_t slice_z) :
        upper_layer(nullptr), lower_layer(nullptr), slicing_errors(false),
        slice_z(slice_z), print_z(print_z), height(height),
        m_id(id), m_object(object) {}
    virtual ~Layer();

private:
    // Sequential index of layer, 0-based, offsetted by number of raft layers.
    size_t              m_id;
    PrintObject        *m_object;
    LayerRegionPtrs     m_regions;
};

class SupportLayer : public Layer 
{
public:
    // Polygons covered by the supports: base, interface and contact areas.
    // Used to suppress retraction if moving for a support extrusion over these support_islands.
    ExPolygonCollection         support_islands;
    // Extrusion paths for the support base and for the support interface and contacts.
    ExtrusionEntityCollection   support_fills;


    // Is there any valid extrusion assigned to this LayerRegion?
    virtual bool                has_extrusions() const { return ! support_fills.empty(); }

    // Zero based index of an interface layer, used for alternating direction of interface / contact layers.
    size_t                      interface_id() const { return m_interface_id; }

protected:
    friend class PrintObject;

    // The constructor has been made public to be able to insert additional support layers for the skirt or a wipe tower
    // between the raft and the object first layer.
    SupportLayer(size_t id, size_t interface_id, PrintObject *object, coordf_t height, coordf_t print_z, coordf_t slice_z) :
        Layer(id, object, height, print_z, slice_z), m_interface_id(interface_id) {}
    virtual ~SupportLayer() = default;

    size_t m_interface_id;
};

template<typename LayerContainer>
inline std::vector<float> zs_from_layers(const LayerContainer &layers)
{
    std::vector<float> zs;
    zs.reserve(layers.size());
    for (const Layer *l : layers)
        zs.emplace_back((float)l->slice_z);
    return zs;
}

extern BoundingBox get_extents(const LayerRegion &layer_region);
extern BoundingBox get_extents(const LayerRegionPtrs &layer_regions);

}

#endif
