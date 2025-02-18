#ifndef PYGS_SCENE_CAMERA_H
#define PYGS_SCENE_CAMERA_H

#include <glm/glm.hpp>

namespace pygs {

class Camera {
 public:
  Camera();
  ~Camera();

  auto Near() const noexcept { return near_; }
  auto Far() const noexcept { return far_; }

  void SetWindowSize(uint32_t width, uint32_t height);

  glm::mat4 ProjectionMatrix() const;
  glm::mat4 ViewMatrix() const;
  glm::vec3 Eye() const;
  uint32_t width() const noexcept { return width_; }
  uint32_t height() const noexcept { return height_; }

  void Rotate(float x, float y);
  void Translate(float x, float y);
  void Zoom(float x);

 private:
  uint32_t width_ = 256;
  uint32_t height_ = 256;
  float fovy_ = glm::radians(60.f);
  float near_ = 0.01f;
  float far_ = 100.f;

  glm::vec3 center_ = {0.f, 0.f, 0.f};
  // camera = center + r (sin phi sin theta, cos phi, sin phi cos theta)
  float r_ = 2.f;
  float phi_ = glm::radians(90.f);
  float theta_ = 0.f;

  float rotation_sensitivity_ = 0.01f;
  float translation_sensitivity_ = 0.002f;
  float zoom_sensitivity_ = 0.01f;
};

}  // namespace pygs

#endif  // PYGS_SCENE_CAMERA_H
