idf_component_register(
    SRCS "yourdesk_v1.c"
         "tm1650_height_decoder.c"
         "yourdesk_preset_logic.c"
         "yourdesk_panel_arbiter.c"
         "yourdesk_panel_proxy.c"
         "yourdesk_soft_i2c_sm.c"
         "yourdesk_soft_i2c_sampler.c"
         "yourdesk_soft_i2c_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES desk_driver esp_driver_i2c esp_driver_gpio driver freertos ulp
)

# Keep protocol switches self-contained; the same sources are also built for ULP.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -fno-jump-tables
    -fno-tree-switch-conversion
)

# The ULP program reuses the same pure state machine and sampled-line frontend
# as the host tests. Only the GPIO/mailbox shell is ULP-specific.
set(ulp_app_name yourdesk_i2c_ulp)
set(ulp_riscv_sources
    "ulp/yourdesk_i2c_main.c"
    "yourdesk_soft_i2c_sm.c"
    "yourdesk_soft_i2c_sampler.c"
)
set(ulp_exp_dep_srcs "yourdesk_soft_i2c_esp.c")
ulp_embed_binary(${ulp_app_name} "${ulp_riscv_sources}" "${ulp_exp_dep_srcs}")
