#ifndef PYGS_ENGINE_VULKAN_CONTEXT_H
#define PYGS_ENGINE_VULKAN_CONTEXT_H

#include <memory>
#include <vector>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include <vulkan/vulkan.h>

#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#endif

#include "vk_mem_alloc.h"

namespace pygs {
namespace vk {

class Context {
 public:
  Context();

  explicit Context(int);

  ~Context();

  VkInstance instance() const;
  VkPhysicalDevice physical_device() const;
  VkDevice device() const;
  uint32_t queue_family_index() const;
  VkQueue queue() const;
  VmaAllocator allocator() const;
  VkCommandPool command_pool() const;
  VkDescriptorPool descriptor_pool() const;

  VkResult GetMemoryFdKHR(const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd);
  VkResult GetSemaphoreFdKHR(const VkSemaphoreGetFdInfoKHR* pGetFdInfo,
                             int* pFd);

#ifdef _WIN32
  VkResult Context::GetMemoryWin32HandleKHR(
      const VkMemoryGetWin32HandleInfoKHR* pGetFdInfo, HANDLE* handle);
  VkResult Context::GetSemaphoreWin32HandleKHR(
      const VkSemaphoreGetWin32HandleInfoKHR* pGetFdInfo, HANDLE* handle);
#endif

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace vk
}  // namespace pygs

#endif  // PYGS_ENGINE_VULKAN_CONTEXT_H
