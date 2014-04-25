/******************************************************************************
 * guacamole - delicious VR                                                   *
 *                                                                            *
 * Copyright: (c) 2011-2013 Bauhaus-Universität Weimar                        *
 * Contact:   felix.lauer@uni-weimar.de / simon.schneegans@uni-weimar.de      *
 *                                                                            *
 * This program is free software: you can redistribute it and/or modify it    *
 * under the terms of the GNU General Public License as published by the Free *
 * Software Foundation, either version 3 of the License, or (at your option)  *
 * any later version.                                                         *
 *                                                                            *
 * This program is distributed in the hope that it will be useful, but        *
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY *
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License   *
 * for more details.                                                          *
 *                                                                            *
 * You should have received a copy of the GNU General Public License along    *
 * with this program. If not, see <http://www.gnu.org/licenses/>.             *
 *                                                                            *
 ******************************************************************************/

#ifndef GUA_GBUFFER_PASS_HPP
#define GUA_GBUFFER_PASS_HPP

// guacamole headers
#include <gua/renderer/GeometryPass.hpp>
#include <gua/renderer/GeometryRessource.hpp>

#include <unordered_map>

namespace gua {

class Pipeline;
class SceneGraph;
class UberShader;

/**
 *
 */
class GBufferPass : public GeometryPass {
 public:

  /**
   *
   */
  GBufferPass(Pipeline* pipeline);

  /**
   * Destructor.
   *
   * Deletes the FullscreenPass and frees all associated data.
   */
  virtual ~GBufferPass();

  void create(
      RenderContext const& ctx,
      std::vector<std::pair<BufferComponent,
                            scm::gl::sampler_state_desc> > const& layers);

  void apply_material_mapping(std::set<std::string> const& materials);

  LayerMapping const* get_gbuffer_mapping() const;

 private:

  void rendering(SerializedScene const& scene,
                 SceneGraph const&,
                 RenderContext const& ctx,
                 CameraMode eye,
                 Camera const& camera,
                 FrameBufferObject* target);

  /**
  * all ubershaders used in scene
  */
  std::unordered_map<std::type_index, UberShader*> ubershaders_;

  /**
  * copy of all material names in scene - used to generate gbuffermappings of ubershaders
  */
  std::set<std::string> materials_;

  /**
   * Ugly hack! "bfc" means backface culling.
   * TODO: implement per material culling
   */
  scm::gl::rasterizer_state_ptr bfc_rasterizer_state_;
  scm::gl::rasterizer_state_ptr no_bfc_rasterizer_state_;

  scm::gl::rasterizer_state_ptr bbox_rasterizer_state_;
  scm::gl::depth_stencil_state_ptr depth_stencil_state_;

  std::shared_ptr<GeometryRessource> bounding_box_;
};

}

#endif  // GUA_GBUFFER_PASS_HPP
