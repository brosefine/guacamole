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

// class header
#include <gua/renderer/ClearPass.hpp>

#include <gua/renderer/GBuffer.hpp>
#include <gua/renderer/ABuffer.hpp>
#include <gua/renderer/WarpRenderer.hpp>
#include <gua/renderer/Pipeline.hpp>
#include <gua/databases/GeometryDatabase.hpp>
#include <gua/databases/Resources.hpp>
#include <gua/utils/Logger.hpp>

#include <boost/variant.hpp>

namespace gua {

////////////////////////////////////////////////////////////////////////////////
ClearPassDescription::ClearPassDescription()
  : PipelinePassDescription() {
  vertex_shader_ = "";
  fragment_shader_ = "";
  name_ = "ClearPass";
  needs_color_buffer_as_input_ = false;
  writes_only_color_buffer_ = false;
  rendermode_ = RenderMode::Custom;
}

////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<PipelinePassDescription> ClearPassDescription::make_copy() const {
  return std::make_shared<ClearPassDescription>(*this);
}

////////////////////////////////////////////////////////////////////////////////
PipelinePass ClearPassDescription::make_pass(RenderContext const& ctx, SubstitutionMap& substitution_map) {
  PipelinePass pass{*this, ctx, substitution_map};
  pass.process_ = [](PipelinePass& pass, PipelinePassDescription const& desc, Pipeline & pipe) {
    auto gbuffer = dynamic_cast<GBuffer*>(pipe.current_viewstate().target);
    if (gbuffer) {
      auto& ctx(pipe.get_context());
      gbuffer->clear_all(ctx, 1.f, 1);
      gbuffer->clear_abuffer(ctx);
    }
  };

  return pass;
}

}