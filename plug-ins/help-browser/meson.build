if get_option('webkit-unmaintained')

plugin_name = 'help-browser'

plugin_sources = [
  'dialog.c',
  'help-browser.c',
  'uri.c',
]

if platform_windows
  plugin_sources += windows.compile_resources(
    gimp_plugins_rc,
    args: [
      '--define', 'ORIGINALFILENAME_STR="@0@"'.format(plugin_name+'.exe'),
      '--define', 'INTERNALNAME_STR="@0@"'    .format(plugin_name),
      '--define', 'TOP_SRCDIR="@0@"'          .format(meson.source_root()),
    ],
    include_directories: [
      rootInclude, appInclude,
    ],
  )
endif

executable(plugin_name,
  plugin_sources,
  dependencies: [
    libgimpui_dep,
    gio,
    webkit,
  ],
  link_with: [
    help_plugin_lib,
  ],
  install: true,
  install_dir: gimpplugindir / 'plug-ins' / plugin_name,
)

endif
