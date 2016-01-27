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
#include <gua/renderer/WarpRenderer.hpp>

#include <gua/renderer/WarpPass.hpp>
#include <gua/config.hpp>

#include <gua/renderer/ResourceFactory.hpp>
#include <gua/renderer/WarpGridGenerator.hpp>
#include <gua/renderer/Pipeline.hpp>
#include <gua/renderer/GBuffer.hpp>
#include <gua/renderer/ABuffer.hpp>
#include <gua/renderer/ResolvePass.hpp>
#include <gua/renderer/opengl_debugging.hpp>
#include <gua/databases/Resources.hpp>
#include <gua/databases/MaterialShaderDatabase.hpp>
#include <gua/databases/WindowDatabase.hpp>

#include <scm/core/math/math.h>

namespace gua {

////////////////////////////////////////////////////////////////////////////////

WarpRenderer::WarpRenderer()
{}

////////////////////////////////////////////////////////////////////////////////

WarpRenderer::~WarpRenderer() {
  if (color_buffer_) {
    color_buffer_->make_non_resident(pipe_->get_context());
  }
  if (depth_buffer_) {
    depth_buffer_->make_non_resident(pipe_->get_context());
  }
  if (hole_filling_texture_) {
    hole_filling_texture_->make_non_resident(pipe_->get_context());
  }
}

////////////////////////////////////////////////////////////////////////////////

void WarpRenderer::render(Pipeline& pipe, PipelinePassDescription const& desc)
{

  auto& ctx(pipe.get_context());
  pipe_ = &pipe;
  auto description(dynamic_cast<WarpPassDescription const*>(&desc));
  math::vec2ui resolution(pipe.current_viewstate().camera.config.get_resolution());

  // ---------------------------------------------------------------------------
  // ------------------------------ allocate resources -------------------------
  // ---------------------------------------------------------------------------

  if (!warp_gbuffer_program_) {

    #ifdef GUACAMOLE_RUNTIME_PROGRAM_COMPILATION
      ResourceFactory factory;
      std::string v_shader = factory.read_shader_file("shaders/warp_gbuffer.vert");
      std::string f_shader = factory.read_shader_file("shaders/warp_gbuffer.frag");
      std::string g_shader = factory.read_shader_file("shaders/warp_gbuffer.geom");
    #else
      std::string v_shader = Resources::lookup_shader("shaders/warp_gbuffer.vert");
      std::string f_shader = Resources::lookup_shader("shaders/warp_gbuffer.frag");
      std::string g_shader = Resources::lookup_shader("shaders/warp_gbuffer.geom");
    #endif

    warp_gbuffer_program_stages_.push_back(ShaderProgramStage(scm::gl::STAGE_VERTEX_SHADER,   v_shader));
    warp_gbuffer_program_stages_.push_back(ShaderProgramStage(scm::gl::STAGE_GEOMETRY_SHADER, g_shader));
    warp_gbuffer_program_stages_.push_back(ShaderProgramStage(scm::gl::STAGE_FRAGMENT_SHADER, f_shader));
    warp_gbuffer_program_ = std::make_shared<ShaderProgram>();
    warp_gbuffer_program_->set_shaders(warp_gbuffer_program_stages_, std::list<std::string>(), false, global_substitution_map_);


    #ifdef GUACAMOLE_RUNTIME_PROGRAM_COMPILATION
      v_shader = factory.read_shader_file("shaders/warp_abuffer.vert");
      f_shader = factory.read_shader_file("shaders/warp_abuffer.frag");
    #else
      v_shader = Resources::lookup_shader("shaders/warp_abuffer.vert");
      f_shader = Resources::lookup_shader("shaders/warp_abuffer.frag");
    #endif

      warp_abuffer_program_stages_.push_back(ShaderProgramStage(scm::gl::STAGE_VERTEX_SHADER,   v_shader));
      warp_abuffer_program_stages_.push_back(ShaderProgramStage(scm::gl::STAGE_FRAGMENT_SHADER, f_shader));
      warp_abuffer_program_ = std::make_shared<ShaderProgram>();
      warp_abuffer_program_->set_shaders(warp_abuffer_program_stages_, std::list<std::string>(), false, global_substitution_map_);

    scm::gl::rasterizer_state_desc p_desc;
    p_desc._point_state = scm::gl::point_raster_state(true);
    points_ = ctx.render_device->create_rasterizer_state(p_desc);

    depth_stencil_state_yes_ = ctx.render_device->create_depth_stencil_state(
        true, true, scm::gl::COMPARISON_LESS, true, 1, 0,
        scm::gl::stencil_ops(scm::gl::COMPARISON_EQUAL)
    );

    depth_stencil_state_no_ = ctx.render_device->create_depth_stencil_state(
        false, false, scm::gl::COMPARISON_LESS, true, 1, 0,
        scm::gl::stencil_ops(scm::gl::COMPARISON_EQUAL)
    );

    auto empty_vbo = ctx.render_device->create_buffer(scm::gl::BIND_VERTEX_BUFFER, scm::gl::USAGE_STATIC_DRAW, sizeof(math::vec2), 0);
    empty_vao_ = ctx.render_device->create_vertex_array(scm::gl::vertex_format(0, 0, scm::gl::TYPE_VEC2F, sizeof(math::vec2)), {empty_vbo});

    scm::gl::sampler_state_desc state(scm::gl::FILTER_MIN_MAG_NEAREST,
      scm::gl::WRAP_CLAMP_TO_EDGE,
      scm::gl::WRAP_CLAMP_TO_EDGE);

    color_buffer_ = std::make_shared<Texture2D>(resolution.x, resolution.y, scm::gl::FORMAT_RGBA_32F, 0, state);
    depth_buffer_ = std::make_shared<Texture2D>(resolution.x, resolution.y, scm::gl::FORMAT_D24,      0, state);

    fbo_ = ctx.render_device->create_frame_buffer();
    fbo_->attach_color_buffer(0, color_buffer_->get_buffer(ctx),0,0);
    fbo_->attach_depth_stencil_buffer(depth_buffer_->get_buffer(ctx),0,0);

    if (description->hole_filling_mode() == WarpPassDescription::HOLE_FILLING_BLUR) {
      int holefilling_levels = 6;
      scm::gl::sampler_state_desc state(scm::gl::FILTER_MIN_MAG_MIP_LINEAR,
        scm::gl::WRAP_CLAMP_TO_EDGE,
        scm::gl::WRAP_CLAMP_TO_EDGE);
      hole_filling_texture_ = std::make_shared<Texture2D>(resolution.x/2, resolution.y/2,
        scm::gl::FORMAT_RGBA_32F, holefilling_levels, state);

      for (int i(0); i<holefilling_levels; ++i) {
        hole_filling_texture_fbos_.push_back(ctx.render_device->create_frame_buffer());
        hole_filling_texture_fbos_.back()->attach_color_buffer(0, hole_filling_texture_->get_buffer(ctx),i,0);
      }

      #ifdef GUACAMOLE_RUNTIME_PROGRAM_COMPILATION
        v_shader = factory.read_shader_file("shaders/common/fullscreen_quad.vert");
        f_shader = factory.read_shader_file("shaders/hole_filling_texture.frag");
      #else
        v_shader = Resources::lookup_shader("shaders/common/fullscreen_quad.vert");
        f_shader = Resources::lookup_shader("shaders/hole_filling_texture.frag");
      #endif

      hole_filling_texture_program_ = std::make_shared<ShaderProgram>();
      hole_filling_texture_program_->create_from_sources(v_shader, f_shader);
    }
  }

  ctx.render_context->set_depth_stencil_state(depth_stencil_state_yes_, 1);

  // get warp matrix -----------------------------------------------------------
  auto gbuffer = dynamic_cast<GBuffer*>(pipe.current_viewstate().target);
  auto stereo_type(pipe.current_viewstate().camera.config.stereo_type());

  bool first_eye(
       (stereo_type == StereoType::SPATIAL_WARP  && ctx.mode != CameraMode::RIGHT)
    || (stereo_type == StereoType::TEMPORAL_WARP && (ctx.framecount % 2 == 0) == (ctx.mode == CameraMode::RIGHT))
  );

  bool first_warp(
       (stereo_type == StereoType::SPATIAL_WARP && first_eye)
    || (stereo_type == StereoType::TEMPORAL_WARP && !first_eye)
  );

  if (first_eye) {
    WarpPassDescription::WarpState state;

    if (!description->get_warp_state()(state)) {
      SceneGraph const* graph(pipe.current_viewstate().graph);
      node::SerializedCameraNode camera(pipe.current_viewstate().camera);

      state.center = camera.get_rendering_frustum(*graph, gua::CameraMode::CENTER, true);
      state.left   = camera.get_rendering_frustum(*graph, gua::CameraMode::LEFT, true);
      state.right  = camera.get_rendering_frustum(*graph, gua::CameraMode::RIGHT, true);
    }

    cached_warp_state_ = state;
  }
 
  gua::math::mat4d proj;
  gua::math::mat4d view;
  gua::math::mat4d warp;

  if (first_eye && stereo_type == StereoType::TEMPORAL_WARP) {
    proj = last_frustum_.get_projection();
    view = last_frustum_.get_view();
    warp = cached_warp_state_.get(ctx.mode);
  } else {
    proj = pipe.current_viewstate().frustum.get_projection();
    view = pipe.current_viewstate().frustum.get_view();
    warp = cached_warp_state_.get(ctx.mode);
  }

  if (first_warp) {
    gbuffer->bind(ctx, false, false, true);
    last_frustum_ = pipe.current_viewstate().frustum;
  }

  math::mat4f warp_matrix(warp * scm::math::inverse(proj * view));
  math::mat4f inv_warp_matrix(scm::math::inverse(warp * scm::math::inverse(proj * view)));

  // ---------------------------------------------------------------------------
  // --------------------------------- warp gbuffer ----------------------------
  // ---------------------------------------------------------------------------
  GUA_PUSH_GL_RANGE(ctx, "Create A-Buffer BVH");
  if (first_warp) {
    gbuffer->get_abuffer().update_min_max_buffer();
  }
  GUA_POP_GL_RANGE(ctx);

  GUA_PUSH_GL_RANGE(ctx, "Warp G-Buffer");

  std::string const gpu_query_name_a = "GPU: Camera uuid: " + std::to_string(pipe.current_viewstate().viewpoint_uuid) + " / WarpPass GBuffer";
  std::string const pri_query_name_a = "Camera uuid: " + std::to_string(pipe.current_viewstate().viewpoint_uuid) + " / WarpPass GBuffer";
  pipe.begin_gpu_query(ctx, gpu_query_name_a);
  pipe.begin_primitive_query(ctx, pri_query_name_a);

  ctx.render_context->set_frame_buffer(fbo_);
  gbuffer->set_viewport(ctx);
  ctx.render_context->clear_color_buffers(
      fbo_, scm::math::vec4f(0,0,0,0));
  ctx.render_context->clear_depth_stencil_buffer(fbo_);

  warp_gbuffer_program_->use(ctx);
  warp_gbuffer_program_->apply_uniform(ctx, "warp_matrix", warp_matrix);

  pipe.bind_gbuffer_input(warp_gbuffer_program_);

  warp_gbuffer_program_->set_uniform(ctx,
                      gbuffer->get_color_buffer()->get_handle(ctx),
                      "gua_gbuffer_color");
  warp_gbuffer_program_->set_uniform(ctx,
                      gbuffer->get_pbr_buffer_write()->get_handle(ctx),
                      "gua_gbuffer_pbr");
  warp_gbuffer_program_->set_uniform(ctx,
                      gbuffer->get_depth_buffer()->get_handle(ctx),
                      "gua_gbuffer_depth");

  auto res(ctx.resources.get_dont_create<WarpGridGenerator::SharedResource>());
  if (res) {
    warp_gbuffer_program_->set_uniform(ctx, res->surface_detection_buffer->get_handle(ctx),  "gua_warp_grid_tex");
    ctx.render_context->bind_vertex_array(res->grid_vao[res->current_vbo()]);
    ctx.render_context->apply();
    ctx.render_context->draw_transform_feedback(scm::gl::PRIMITIVE_POINT_LIST, res->grid_tfb[res->current_vbo()]);
  }

  pipe.end_primitive_query(ctx, pri_query_name_a);
  pipe.end_gpu_query(ctx, gpu_query_name_a);

  GUA_POP_GL_RANGE(ctx);

  // ---------------------------------------------------------------------------
  // --------------------- warp abuffer & hole filling -------------------------
  // ---------------------------------------------------------------------------

  if (description->hole_filling_mode() == WarpPassDescription::HOLE_FILLING_BLUR) {
    GUA_PUSH_GL_RANGE(ctx, "Generate Hole Filling Map");

    std::string const gpu_query_name_c = "GPU: Camera uuid: " + std::to_string(pipe.current_viewstate().viewpoint_uuid) + " / WarpPass Generate Hole Filling Texture";
    pipe.begin_gpu_query(ctx, gpu_query_name_c);

    ctx.render_context->set_depth_stencil_state(depth_stencil_state_no_, 1);

    hole_filling_texture_program_->use(ctx);
    hole_filling_texture_program_->set_uniform(ctx, color_buffer_->get_handle(ctx), "color_buffer");
    hole_filling_texture_program_->set_uniform(ctx, depth_buffer_->get_handle(ctx), "depth_buffer");
    hole_filling_texture_program_->set_uniform(ctx, hole_filling_texture_->get_handle(ctx), "hole_filling_texture");

    for (int i(0); i<hole_filling_texture_fbos_.size(); ++i) {
      math::vec2ui level_size(scm::gl::util::mip_level_dimensions(math::vec2ui(hole_filling_texture_->width(), hole_filling_texture_->height()), i));
      ctx.render_context->set_frame_buffer(hole_filling_texture_fbos_[i]);
      ctx.render_context->set_viewport(scm::gl::viewport(scm::math::vec2f(0, 0), scm::math::vec2f(level_size)));
      hole_filling_texture_program_->set_uniform(ctx, i, "current_level");
      pipe_->draw_quad();
    }

    pipe.end_gpu_query(ctx, gpu_query_name_c);
    GUA_POP_GL_RANGE(ctx);
  }


  
  gbuffer->set_viewport(ctx);

  GUA_PUSH_GL_RANGE(ctx, "Warp A-Buffer");

  std::string const gpu_query_name_b = "GPU: Camera uuid: " + std::to_string(pipe.current_viewstate().viewpoint_uuid) + " / WarpPass ABuffer";
  pipe.begin_gpu_query(ctx, gpu_query_name_b);

  warp_abuffer_program_->use(ctx);
  warp_abuffer_program_->apply_uniform(ctx, "warp_matrix", warp_matrix);
  warp_abuffer_program_->apply_uniform(ctx, "inv_warp_matrix", inv_warp_matrix);

  // Not very beautiful: we need to set the tonemapping exposure. This is a uniform set by the resolve pass,
  // so we have to find the resolvepass to get that value
  float exposure(pipe.current_viewstate().camera.pipeline_description->get_pass_by_type<gua::ResolvePassDescription>()->tone_mapping_exposure());
  warp_abuffer_program_->apply_uniform(ctx, "gua_tone_mapping_exposure", exposure);

  gbuffer->get_abuffer().bind_min_max_buffer(warp_abuffer_program_);

  bool write_all_layers = false;
  bool do_clear = false;
  bool do_swap = false;
  gbuffer->bind(ctx, write_all_layers, do_clear, do_swap);

  if (description->hole_filling_mode() == WarpPassDescription::HOLE_FILLING_BLUR) {
    warp_abuffer_program_->set_uniform(ctx, hole_filling_texture_->get_handle(ctx), "hole_filling_texture");
  }

  warp_abuffer_program_->set_uniform(ctx, color_buffer_->get_handle(ctx), "warped_color_buffer");
  warp_abuffer_program_->set_uniform(ctx, depth_buffer_->get_handle(ctx), "warped_depth_buffer");

  ctx.render_context->set_depth_stencil_state(depth_stencil_state_no_, 1);
  ctx.render_context->apply();

  pipe.draw_quad();

  pipe.end_gpu_query(ctx, gpu_query_name_b);

  GUA_POP_GL_RANGE(ctx);

  gbuffer->unbind(ctx);

  ctx.render_context->reset_state_objects();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace gua