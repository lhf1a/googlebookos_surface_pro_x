/* Count simultaneous logical outputs from real connector -> encoder -> CRTC routes. */
#pragma once
#include <xf86drmMode.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace qti_dpu {
struct DisplayRoute { int kind; std::vector<uint32_t> encoders; };
struct EncoderRoute { uint32_t id; uint32_t crtcs; };

inline int ConnectorKind(uint32_t type) {
  switch (type) {
    case DRM_MODE_CONNECTOR_eDP:
    case DRM_MODE_CONNECTOR_DSI: return 0;
    case DRM_MODE_CONNECTOR_TV:
    case DRM_MODE_CONNECTOR_HDMIA:
    case DRM_MODE_CONNECTOR_HDMIB:
    case DRM_MODE_CONNECTOR_DisplayPort:
    case DRM_MODE_CONNECTOR_VGA: return 1;
    case DRM_MODE_CONNECTOR_VIRTUAL:
    case DRM_MODE_CONNECTOR_WRITEBACK: return 2;
    default: return -1;
  }
}

inline int CountDisplayRoutes(const std::vector<DisplayRoute> &connectors,
                              const std::vector<EncoderRoute> &encoders, uint32_t crtcs,
                              std::array<int32_t, 4> *counts) {
  if (!counts || crtcs > 32) return -EINVAL;  // DRM possible_crtcs is a 32-bit mask.
  *counts = {};
  std::map<uint32_t, size_t> encoder_index;
  for (size_t i = 0; i < encoders.size(); ++i) {
    if (!encoders[i].id || !encoder_index.emplace(encoders[i].id, i).second) return -EINVAL;
    if (crtcs < 32 && (encoders[i].crtcs >> crtcs)) return -EINVAL;
  }
  for (const auto &connector : connectors) {
    if (connector.kind < 0 || connector.kind > 2) return -EINVAL;
    for (uint32_t id : connector.encoders) if (!encoder_index.count(id)) return -ENOENT;
  }
  // Capacity-one encoder and CRTC vertices avoid counting alternative routes as extra displays.
  struct Edge { size_t to; size_t reverse; unsigned capacity; };
  for (int kind = 0; kind < 4; ++kind) {
    const size_t encoder_base = 1 + connectors.size();
    const size_t crtc_base = encoder_base + encoders.size() * 2;
    const size_t sink = crtc_base + crtcs;
    std::vector<std::vector<Edge>> graph(sink + 1);
    auto add = [&](size_t from, size_t to) {
      Edge forward{to, graph[to].size(), 1};
      Edge reverse{from, graph[from].size(), 0};
      graph[from].push_back(forward);
      graph[to].push_back(reverse);
    };
    for (size_t i = 0; i < connectors.size(); ++i) {
      if (kind != 3 && connectors[i].kind != kind) continue;
      add(0, i + 1);
      for (uint32_t id : connectors[i].encoders) add(i + 1, encoder_base + 2 * encoder_index.at(id));
    }
    for (size_t i = 0; i < encoders.size(); ++i) {
      const size_t node = encoder_base + 2 * i;
      add(node, node + 1);
      for (uint32_t crtc = 0; crtc < crtcs; ++crtc)
        if (encoders[i].crtcs & (uint32_t{1} << crtc)) add(node + 1, crtc_base + crtc);
    }
    for (uint32_t crtc = 0; crtc < crtcs; ++crtc) add(crtc_base + crtc, sink);
    for (;;) {
      const size_t unset = std::numeric_limits<size_t>::max();
      std::vector<std::pair<size_t, size_t>> parent(graph.size(), {unset, unset});
      std::vector<size_t> queue{0};
      parent[0] = {0, 0};
      for (size_t next = 0; next < queue.size() && parent[sink].first == unset; ++next) {
        size_t from = queue[next];
        for (size_t edge = 0; edge < graph[from].size(); ++edge) {
          const auto &link = graph[from][edge];
          if (!link.capacity || parent[link.to].first != unset) continue;
          parent[link.to] = {from, edge};
          queue.push_back(link.to);
        }
      }
      if (parent[sink].first == unset) break;
      for (size_t to = sink; to; to = parent[to].first) {
        auto &edge = graph[parent[to].first][parent[to].second];
        --edge.capacity;
        ++graph[to][edge.reverse].capacity;
      }
      ++(*counts)[kind];
    }
  }
  return 0;
}

inline int ReadDisplayCounts(int fd, std::array<int32_t, 4> *counts) {
  errno = 0;
  std::unique_ptr<drmModeRes, decltype(&drmModeFreeResources)> resources(
      drmModeGetResources(fd), drmModeFreeResources);
  if (!resources) return -(errno ? errno : EIO);
  if (resources->count_crtcs < 0 || resources->count_crtcs > 32 ||
      resources->count_encoders < 0 || resources->count_connectors < 0) return -EINVAL;
  std::vector<EncoderRoute> encoders;
  std::vector<DisplayRoute> connectors;
  for (int i = 0; i < resources->count_encoders; ++i) {
    errno = 0;
    std::unique_ptr<drmModeEncoder, decltype(&drmModeFreeEncoder)> encoder(
        drmModeGetEncoder(fd, resources->encoders[i]), drmModeFreeEncoder);
    if (!encoder) return -(errno ? errno : EIO);
    encoders.push_back({encoder->encoder_id, encoder->possible_crtcs});
  }
  for (int i = 0; i < resources->count_connectors; ++i) {
    errno = 0;
    std::unique_ptr<drmModeConnector, decltype(&drmModeFreeConnector)> connector(
        drmModeGetConnector(fd, resources->connectors[i]), drmModeFreeConnector);
    if (!connector) return -(errno ? errno : EIO);
    if (connector->count_encoders < 0) return -EINVAL;
    int kind = ConnectorKind(connector->connector_type);
    if (kind < 0) continue;
    DisplayRoute route{kind, {}};
    for (int j = 0; j < connector->count_encoders; ++j) route.encoders.push_back(connector->encoders[j]);
    connectors.push_back(route);
  }
  return CountDisplayRoutes(connectors, encoders, uint32_t(resources->count_crtcs), counts);
}
}  // namespace qti_dpu
