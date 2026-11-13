#ifndef PS2_GS_VULKAN_BRIDGE_H
#define PS2_GS_VULKAN_BRIDGE_H

// SVR06: bridge from the runtime's GS front end to paraLLEl-GS (Vulkan compute GS emulation, LGPL-3.0+).
// Vulkan is the only player-facing renderer. When the project is built without PS2X_HAS_PARALLEL_GS every
// call is an inert stub and enabled() returns false.

#include <cstdint>
#include <vector>

struct GSRegisters;

namespace gsvk
{
// True when the Vulkan GS is selected (SVR06_GS=vulkan) and the bridge was compiled in.
bool enabled();
// Lazily creates the Vulkan device + GSInterface. Returns false (and logs once) on failure.
bool ensureInit();
// Remember which GIF path (1..3) the next packet submitted from this thread belongs to.
void setSubmitPath(uint32_t path);
uint32_t submitPath();
// Forward one GIF packet (size must be a multiple of 16) to paraLLEl-GS.
void gifTransfer(uint32_t path, const uint8_t *data, uint32_t sizeBytes);
// EE->GS host-to-local image upload that bypassed the GIF packet path.
void writeReg(uint8_t reg, uint64_t value);
void uploadImage(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg, uint64_t trxdir,
                 const uint8_t *data, uint32_t sizeBytes);
// GS->host readback (sceGsExecStoreImage): copies a PSMCT32 rectangle out of the Vulkan GS local memory into linear host rows.
// Returns the number of bytes written (0 on failure / unsupported format).
uint32_t readLocalToHostCT32(uint32_t sbp, uint32_t sbw, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint8_t *dst, uint32_t maxBytes);
bool readVramPages(uint32_t firstPage, uint32_t numPages, uint8_t *dst); // [gsshadow] raw 8 KB pages of paraLLEl-GS local memory
// Flush, vsync and read the scanned-out frame back as tightly packed RGBA8.
// Returns false if no image is available this vsync.
bool presentFrame(const GSRegisters &privRegs, std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height);
int outputScale(); // [scale] 1 or 2: host presentation frame is 640x512 times this
bool lastPresentWasQueueFill(); // [nopink] CL-0115: true when the last presentFrame() returned false only because the frame queue was refilling
} // namespace gsvk

#endif
