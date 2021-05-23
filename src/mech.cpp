#include "mech.hpp"

#include "ZD/3rd/glm/gtx/rotate_vector.hpp"
#include "ZD/3rd/glm/ext/quaternion_trigonometric.hpp"
#include "ZD/3rd/glm/gtx/quaternion.hpp"

#include "world.hpp"

glm::quat rotation_between_vectors(glm::vec3 start, glm::vec3 dest)
{
  start = normalize(start);
  dest = normalize(dest);

  float cosTheta = dot(start, dest);
  glm::vec3 rotationAxis;

  if (cosTheta < -1.0f + 0.001f)
  {
    rotationAxis = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), start);
    if (glm::length2(rotationAxis) < 0.01f)
      rotationAxis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), start);

    rotationAxis = normalize(rotationAxis);
    return glm::angleAxis((float)M_PI, rotationAxis);
  }

  rotationAxis = cross(start, dest);

  float s = sqrt((1.0f + cosTheta) * 2.0f);
  float invs = 1.0f / s;

  return glm::quat(s * 0.5f, rotationAxis.x * invs, rotationAxis.y * invs, rotationAxis.z * invs);
}

glm::quat rotate_lookat(glm::quat q1, glm::quat q2, float maxAngle)
{
  if (maxAngle < 0.001f)
  {
    return q1;
  }

  float cosTheta = glm::dot(q1, q2);

  if (cosTheta > 0.9999f)
  {
    return q2;
  }

  if (cosTheta < 0)
  {
    q1 = q1 * -1.0f;
    cosTheta *= -1.0f;
  }

  float angle = glm::acos(cosTheta);

  if (angle < maxAngle)
  {
    return q2;
  }

  float fT = maxAngle / angle;
  angle = maxAngle;

  glm::quat res = (glm::sin((1.0f - fT) * angle) * q1 + glm::sin(fT * angle) * q2) / glm::sin(angle);
  res = glm::normalize(res);
  return res;
}

LegPart::LegPart(const size_t part_index)
: ZD::Entity {}
, part_index { part_index }
{
  add_texture(ZD::Texture::load(
    ZD::Image::load("textures/metal29_diffuse.tga"),
    ZD::TextureParameters { .generate_mipmap = true,
                            .mag_filter = GL_LINEAR,
                            .min_filter = GL_LINEAR_MIPMAP_LINEAR,
                            .wrap_mode = GL_REPEAT }));

  std::shared_ptr<ZD::Model> model;
  switch (part_index)
  {
    case 0: model = ZD::Model::load("models/mech_leg_b.obj"); break;
    case 1: model = ZD::Model::load("models/mech_leg_m.obj"); break;
    case 2:
    default: model = ZD::Model::load("models/mech_leg_e.obj"); break;
  }
  add_model(model);
}

Mech::Mech(glm::vec3 position)
: ZD::Entity(position, {}, { 1.0, 1.0, 1.0 })
{
  body = std::make_unique<ZD::Entity>();
  const auto body_model = ZD::Model::load("models/mech_body.obj");
  const auto metal_texture = ZD::Texture::load(
    ZD::Image::load("textures/metal29_diffuse.tga"),
    ZD::TextureParameters { .generate_mipmap = true,
                            .mag_filter = GL_LINEAR,
                            .min_filter = GL_LINEAR_MIPMAP_LINEAR,
                            .wrap_mode = GL_REPEAT });

  body->add_texture(metal_texture);
  body->add_model(body_model);
  body->set_position(position);

  const size_t LEGS_NUM = 4;
  for (size_t i = 0; i < LEGS_NUM; i++)
  {
    legs_b.push_back(std::make_unique<LegPart>(0));
    legs_m.push_back(std::make_unique<LegPart>(1));
    legs_e.push_back(std::make_unique<LegPart>(2));
  }

  shader = ZD::ShaderLoader()
             .add(ZD::File("shaders/model.vertex.glsl"), GL_VERTEX_SHADER)
             .add(ZD::File("shaders/model.fragment.glsl"), GL_FRAGMENT_SHADER)
             .compile();
}

void Mech::step_path(const World &world)
{
  if (path.size() < 2)
    return;

  const auto get_3d_position = [&world](const auto &xz) {
    const float pt_x = xz.first * world.X_SPACING;
    const float pt_z = xz.second * world.Z_SPACING;
    return glm::vec3 { pt_x, world.ground->get_y(pt_x, pt_z), pt_z };
  };

  glm::vec3 closest_path_point = get_3d_position(path.front());
  glm::vec3 next_path_point = get_3d_position(*(path.begin() + 1));

  for (size_t i = 0; i < path.size(); ++i)
  {
    const auto &coord = path[i];
    const glm::vec3 path_point = get_3d_position(coord);
    if (glm::distance(position, path_point) < glm::distance(position, closest_path_point))
    {
      closest_path_point = path_point;
      if (i < path.size() - 1)
        next_path_point = get_3d_position(path[i + 1]);
    }
  }

  if (closest_path_point == next_path_point && glm::distance(next_path_point, position) < 2.0f)
  {
    return;
  }

  glm::vec3 move_vec = glm::normalize(next_path_point - position);
  position += move_vec * 0.1f; // TODO: move speed parameter
  position.y = world.ground->get_y(position.x, position.z) + 2.0f;

  const glm::vec3 FORWARD { 0.0f, 0.0f, 1.0f };
  move_vec.y = 0.0f;
  rotation =
    rotate_lookat(rotation, rotation_between_vectors(FORWARD, move_vec), 0.02f); //TODO: rotation speed parameter
}

void Mech::update([[maybe_unused]] const World &world)
{
  // move and rotate based on path
  step_path(world);
  body->set_position(position);
  body->set_rotation(rotation);

  // find leg end target position
  const float angle_step = 2.0f * M_PI / static_cast<float>(legs_b.size());
  for (size_t i = 0; i < legs_b.size(); ++i)
  {
    const float angle = angle_step / 2.0f + i * angle_step;

    const glm::quat target_rotation = rotation * glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));
    auto target_pos = position + target_rotation * glm::vec3(3.1f, 0.0f, 0.0f);
    target_pos.y = world.ground->get_y(target_pos.x, target_pos.z);

    if (
      glm::distance(
        legs_e[i]->get_position() + legs_e[i]->get_rotation() * glm::vec3 { 1.0f, 0.0f, 0.0f },
        target_pos) > 1.2f)
    {
      legs_e[i]->target_position = target_pos;
    }
  }

  // inverse
  for (size_t i = 0; i < legs_b.size(); ++i)
  {
    const glm::vec3 dir_e = glm::normalize(legs_e[i]->target_position - legs_e[i]->get_position());
    legs_e[i]->set_position(legs_e[i]->target_position - dir_e * 1.0f);
    legs_e[i]->set_rotation(rotation_between_vectors(glm::vec3 { 1.0f, 0.0f, 0.0f }, dir_e));

    const glm::vec3 dir_m = glm::normalize(legs_e[i]->get_position() - legs_m[i]->get_position());
    legs_m[i]->set_position(legs_e[i]->get_position() - dir_m * 1.0f);
    legs_m[i]->set_rotation(rotation_between_vectors(glm::vec3 { 1.0f, 0.0f, 0.0f }, dir_m));

    const glm::vec3 dir_b = glm::normalize(legs_m[i]->get_position() - legs_b[i]->get_position());
    legs_b[i]->set_position(legs_m[i]->get_position() - dir_b * 1.0f);
    legs_b[i]->set_rotation(rotation_between_vectors(glm::vec3 { 1.0f, 0.0f, 0.0 }, dir_b));
  }

  // forward
  for (size_t i = 0; i < legs_b.size(); ++i)
  {
    const glm::vec3 b_translate = position + rotation * glm::vec3 { 0.0f, -1.0f, 0.0f };
    legs_b[i]->set_position(b_translate);

    const glm::vec3 m_translate = b_translate + legs_b[i]->get_rotation() * glm::vec3 { 1.0f, 0.0f, 0.0f };
    legs_m[i]->set_position(m_translate);

    const glm::vec3 e_translate = m_translate + legs_m[i]->get_rotation() * glm::vec3 { 1.0f, 0.0f, 0.0f };
    legs_e[i]->set_position(e_translate);
  }

#if 0
  for (size_t i = 0; i < legs_b.size(); ++i)
  {
    auto &leg_b = legs_b[i];
    const glm::vec3 b_translate = position + rotation * glm::vec3 { 0.0f, -1.0f, 0.0f };
    const glm::quat b_rotate = rotation * leg_b->get_rotation();
    leg_b->set_position(b_translate);
    //leg_b->set_rotation(b_rotate);

    auto &leg_m = legs_m[i];
    const glm::vec3 m_translate = b_translate + leg_b->get_rotation() * glm::vec3 { 1.0f, 0.0f, 0.0f };
    const glm::quat m_rotate = leg_b->get_rotation() * leg_m->get_rotation();
    leg_m->set_position(m_translate);
    //leg_m->set_rotation(m_rotate);

    auto &leg_e = legs_e[i];
    const glm::vec3 e_translate = m_translate + leg_m->get_rotation() * glm::vec3 { 1.0f, 0.0f, 0.0f };
    //const glm::quat e_rotate = m_rotate * leg_e->get_rotation();
    leg_e->set_position(e_translate);
    //leg_e->set_rotation(e_rotate);
  }
#endif
}

void Mech::render(ZD::View &view, [[maybe_unused]] const World &world)
{
  shader->use();
  body->set_position(get_position());
  body->set_rotation(get_rotation());
  body->set_scale(get_scale());
  body->draw(*shader, view);

  for (size_t i = 0; i < legs_b.size(); ++i)
  {
    legs_b[i]->draw(*shader, view);
    legs_m[i]->draw(*shader, view);
    legs_e[i]->draw(*shader, view);
  }
}
