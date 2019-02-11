python_opt = get_option('with-python')
if python_opt.startswith('python3')
  python_dep = dependency(python_opt, version: '>= 3.3')
  flex_dep   = dependency('libfl', static: true)
else
  python_dep = dependency(python_opt, version: '>= 2.7')
  flex_dep   = dependency('libfl', static: true)
endif

shared_module('minpython', 'minpython.c', 'asyncresult.c', 'colorizelexer.yy.c', 
              'console.c', 'context.c', 'delegate.c', 'delegateproxy.c', 
              'eventattrs.c', 'listiter.c', 'outstream.c', 'plugin.c', 
              'subinterp.c', 'maininterp.c', 'interpcall.c', 'interpobjproxy.c',
              'interptypeproxy.c',
  dependencies: [libgio_dep, hexchat_plugin_dep, python_dep, flex_dep],
  install: true,
  install_dir: plugindir,
  name_prefix: '',
)
