// SPDX-License-Identifier: MIT
#include <GenTL.h>

namespace GenTL {
GC_ERROR unsupported_control_operation() noexcept;

// Export the ABI entry points for capabilities this control-only CTI lacks.
#define UNSUPPORTED(name, ...) GC_API name(__VA_ARGS__) { return unsupported_control_operation(); }

UNSUPPORTED(GCRegisterEvent, EVENTSRC_HANDLE, EVENT_TYPE, EVENT_HANDLE *)
UNSUPPORTED(GCUnregisterEvent, EVENTSRC_HANDLE, EVENT_TYPE)
UNSUPPORTED(EventGetData, EVENT_HANDLE, void *, size_t *, uint64_t)
UNSUPPORTED(EventGetDataInfo, EVENT_HANDLE, const void *, size_t, EVENT_DATA_INFO_CMD, INFO_DATATYPE *, void *, size_t *)
UNSUPPORTED(EventGetInfo, EVENT_HANDLE, EVENT_INFO_CMD, INFO_DATATYPE *, void *, size_t *)
UNSUPPORTED(EventFlush, EVENT_HANDLE)
UNSUPPORTED(EventKill, EVENT_HANDLE)
UNSUPPORTED(DSAnnounceBuffer, DS_HANDLE, void *, size_t, void *, BUFFER_HANDLE *)
UNSUPPORTED(DSAllocAndAnnounceBuffer, DS_HANDLE, size_t, void *, BUFFER_HANDLE *)
UNSUPPORTED(DSFlushQueue, DS_HANDLE, ACQ_QUEUE_TYPE)
UNSUPPORTED(DSStartAcquisition, DS_HANDLE, ACQ_START_FLAGS, uint64_t)
UNSUPPORTED(DSStopAcquisition, DS_HANDLE, ACQ_STOP_FLAGS)
UNSUPPORTED(DSGetInfo, DS_HANDLE, STREAM_INFO_CMD, INFO_DATATYPE *, void *, size_t *)
UNSUPPORTED(DSGetBufferID, DS_HANDLE, uint32_t, BUFFER_HANDLE *)
UNSUPPORTED(DSClose, DS_HANDLE)
UNSUPPORTED(DSRevokeBuffer, DS_HANDLE, BUFFER_HANDLE, void **, void **)
UNSUPPORTED(DSQueueBuffer, DS_HANDLE, BUFFER_HANDLE)
UNSUPPORTED(DSGetBufferInfo, DS_HANDLE, BUFFER_HANDLE, BUFFER_INFO_CMD, INFO_DATATYPE *, void *, size_t *)
UNSUPPORTED(DSGetBufferChunkData, DS_HANDLE, BUFFER_HANDLE, SINGLE_CHUNK_DATA *, size_t *)
UNSUPPORTED(DSGetParentDev, DS_HANDLE, DEV_HANDLE *)
UNSUPPORTED(DSGetNumBufferParts, DS_HANDLE, BUFFER_HANDLE, uint32_t *)
UNSUPPORTED(DSGetBufferPartInfo, DS_HANDLE, BUFFER_HANDLE, uint32_t, BUFFER_PART_INFO_CMD, INFO_DATATYPE *, void *, size_t *)
UNSUPPORTED(DSAnnounceCompositeBuffer, DS_HANDLE, size_t, void **, size_t *, void *, BUFFER_HANDLE *)
UNSUPPORTED(DSGetBufferInfoStacked, DS_HANDLE, BUFFER_HANDLE, DS_BUFFER_INFO_STACKED *, size_t)
UNSUPPORTED(DSGetBufferPartInfoStacked, DS_HANDLE, BUFFER_HANDLE, DS_BUFFER_PART_INFO_STACKED *, size_t)
UNSUPPORTED(DSGetNumFlows, DS_HANDLE, uint32_t *)
UNSUPPORTED(DSGetFlowInfo, DS_HANDLE, uint32_t, FLOW_INFO_CMD, INFO_DATATYPE *, void *, size_t *)
UNSUPPORTED(DSGetNumBufferSegments, DS_HANDLE, BUFFER_HANDLE, uint32_t *)
UNSUPPORTED(DSGetBufferSegmentInfo, DS_HANDLE, BUFFER_HANDLE, uint32_t, SEGMENT_INFO_CMD, INFO_DATATYPE *, void *, size_t *)
#undef UNSUPPORTED
} // namespace GenTL
