cmake_minimum_required(VERSION 3.13)
project(tesi_uafx C)

set(CMAKE_C_STANDARD 99)

# ---------------------------------------------------------
# 1. open62541 — compilé UNE SEULE FOIS pour tout le repo
# ---------------------------------------------------------
set(UA_NAMESPACE_ZERO "FULL" CACHE STRING "" FORCE)
set(UA_ENABLE_ENCRYPTION "MBEDTLS" CACHE STRING "" FORCE)
set(UA_ENABLE_PUBSUB ON CACHE BOOL "" FORCE)
set(UA_ENABLE_PUBSUB_SKS ON CACHE BOOL "" FORCE)
set(UA_ENABLE_DISCOVERY_MULTICAST "MDNSD" CACHE STRING "" FORCE)
set(UA_ENABLE_DISCOVERY ON CACHE BOOL "" FORCE)

add_subdirectory(lib/open62541)

set(NODESET_DIR ${UA_NODESET_DIR})

# ---------------------------------------------------------
# 2. Génération des types UAFX — UNE SEULE FOIS aussi
# ---------------------------------------------------------
ua_generate_nodeset_and_datatypes(
    NAME "di"
    FILE_CSV "${NODESET_DIR}/DI/Opc.Ua.Di.NodeIds.csv"
    FILE_BSD "${NODESET_DIR}/DI/Opc.Ua.Di.Types.bsd"
    FILE_NS  "${NODESET_DIR}/DI/Opc.Ua.Di.NodeSet2.xml"
    IMPORT_BSD "UA_TYPES#${NODESET_DIR}/Schema/Opc.Ua.Types.bsd"
    INTERNAL
)

ua_generate_nodeset_and_datatypes(
    NAME "uafx-data"
    FILE_CSV "${NODESET_DIR}/UAFX/opc.ua.fx.data.nodeset2.csv"
    FILE_BSD "${NODESET_DIR}/UAFX/opc.ua.fx.data.nodeset2.bsd"
    FILE_NS  "${NODESET_DIR}/UAFX/opc.ua.fx.data.nodeset2.xml"
    IMPORT_BSD "UA_TYPES#${NODESET_DIR}/Schema/Opc.Ua.Types.bsd"
    IMPORT_BSD "TYPES_DI#${NODESET_DIR}/DI/Opc.Ua.Di.Types.bsd"
    DEPENDS "di"
    INTERNAL
)

ua_generate_nodeset_and_datatypes(
    NAME "uafx-ac"
    FILE_CSV "${NODESET_DIR}/UAFX/opc.ua.fx.ac.nodeset2.csv"
    FILE_BSD "${NODESET_DIR}/UAFX/opc.ua.fx.ac.nodeset2.bsd"
    FILE_NS  "${NODESET_DIR}/UAFX/opc.ua.fx.ac.nodeset2.xml"
    IMPORT_BSD "UA_TYPES#${NODESET_DIR}/Schema/Opc.Ua.Types.bsd"
    IMPORT_BSD "TYPES_DI#${NODESET_DIR}/DI/Opc.Ua.Di.Types.bsd"
    IMPORT_BSD "TYPES_UAFX_DATA#${NODESET_DIR}/UAFX/opc.ua.fx.data.nodeset2.bsd"
    DEPENDS "di" "uafx-data"
    INTERNAL
)

# ---------------------------------------------------------
# 3. uafx_types : une lib réutilisable par toutes les expériences
# ---------------------------------------------------------
add_library(uafx_types STATIC
    ${UA_NODESET_DI_SOURCES}        ${UA_TYPES_DI_SOURCES}
    ${UA_NODESET_UAFX_DATA_SOURCES} ${UA_TYPES_UAFX_DATA_SOURCES}
    ${UA_NODESET_UAFX_AC_SOURCES}   ${UA_TYPES_UAFX_AC_SOURCES}
)
add_dependencies(uafx_types
    open62541-generator-ns-di
    open62541-generator-ns-uafx-data
    open62541-generator-ns-uafx-ac
)
target_include_directories(uafx_types PUBLIC ${CMAKE_BINARY_DIR}/src_generated/open62541)
target_link_libraries(uafx_types PUBLIC open62541::open62541)

# ---------------------------------------------------------
# 4. Expériences
# ---------------------------------------------------------
add_subdirectory(experiments/discovery_fx)
add_subdirectory(experiments/framework/server)
