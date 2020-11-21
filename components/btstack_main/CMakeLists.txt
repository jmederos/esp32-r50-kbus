idf_component_register(SRCS "btstack_main.c"
                    INCLUDE_DIRS "include"
                    REQUIRES avrcp_control btstack bt)

#set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY LINK_INTERFACE_MULTIPLICITY 3)
