# OpenGL ES Module Design

The overall idea of this module is to use the same Vulkan server module. That is, the flow will still go like this:
- Application requests swapchain creation
- Client tells server to create swapchain
- Server creates swapchain and staging images and sends the handles back to the client
- Client imports handles (this time in OpenGL instead of Vulkan)
- Application renders into staging images and releases the image
- Client enqueues a swapchain signal (no layout transitions for OpenGL, just stays in general layout)
- Client tells server it has released the image
- Server enqueues a wait on that semaphore and a copy onto the real swapchain, then a signal for the return semaphore
- Next time the application acquires that same image, it enqueues a wait on that semaphore

The main difference here is that the client does not need to worry about layout transitions or releasing ownership to external queue families. When the client signals the rendering done semaphore that automatically does a full flush and there's no need for an ownership release. When the server acquires ownership from external (which is still necessary for the Vulkan server), it transitions the layout from GENERAL. I should make it so that clients of the Vulkan server module tell the server which layout to transition from.
