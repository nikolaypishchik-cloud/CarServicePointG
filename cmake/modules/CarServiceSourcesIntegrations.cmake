# P2 architecture: source boundary for the integrations layer.
# Keep layer ownership here; the root CMakeLists.txt must remain orchestration-only.

set(CS_INTEGRATIONS_SOURCES
    src/integrations/opendatabot/OpenDataBotClient.cpp
)

set(CS_INTEGRATIONS_HEADERS
    src/integrations/opendatabot/OpenDataBotClient.h
)
