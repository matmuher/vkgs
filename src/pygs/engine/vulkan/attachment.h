#ifndef PYGS_ENGINE_VULKAN_ATTACHMENT_H
#define PYGS_ENGINE_VULKAN_ATTACHMENT_H

#include <memory>

#include <vulkan/vulkan.h>

#include "pygs/engine/vulkan/context.h"
#include "pygs/engine/vulkan/image_spec.h"

namespace pygs {
namespace vk {

class Context;

class Attachment {
 public:
  Attachment();

  Attachment(Context context, uint32_t width, uint32_t height, VkFormat format,
             VkSampleCountFlagBits samples, bool input_attachment);

  ~Attachment();

  operator VkImageView() const;

  VkImage image() const;
  VkImageUsageFlags usage() const;
  VkFormat format() const;
  ImageSpec image_spec() const;

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace vk
}  // namespace pygs

#endif  // PYGS_ENGINE_VULKAN_ATTACHMENT_H
