fs = import('fs')

incdirs = [ '.' ]

src = [ 'ahb.c',
	'ast.c',
	'clk.c',
	'cmd/console.c',
	'cmd/debug.c',
	'cmd/devmem.c',
	'cmd/ilpc.c',
	'cmd/otp.c',
	'cmd/p2a.c',
	'cmd/probe.c',
	'cmd/read.c',
	'cmd/replace.c',
	'cmd/reset.c',
	'cmd/sfc.c',
	'cmd/trace.c',
	'cmd/write.c',
	'debug.c',
	'devmem.c',
	'doit.c',
	'flash.c',
	'ilpc.c',
	'l2a.c',
	'log.c',
	'mmio.c',
	'otp.c',
	'p2a.c',
	'pci.c',
	'priv.c',
	'prompt.c',
	'rev.c',
	'sdmc.c',
	'sfc.c',
	'shell.c',
	'sio.c',
	'soc.c',
	'trace.c',
	'ts16.c',
	'tty.c',
	'uart/mux.c',
	'uart/suart.c',
	'uart/vuart.c',
	'wdt.c' ]

conf_data = configuration_data()

# Version
p = run_command('git', 'describe', '--tags', '--dirty', '--always')
version = p.stdout().strip()
conf_data.set_quoted('version', version)

# Architecture-specific capabilities
host = host_machine.cpu_family()
host_inc = 'arch/@0@'.format(host)
if fs.is_dir(host_inc)
	incdirs += [ host_inc ]
endif

host_lpc = 'arch/@0@/lpc.c'.format(host)
if fs.is_file(host_lpc)
	src += [ host_lpc ]
	conf_data.set10('have_lpc', 1)
else
	conf_data.set10('have_lpc', 0)
endif

configure_file(input: 'config.h.in',
	       output: 'config.h',
	       configuration: conf_data)

subdir('devicetree')

executable('doit', src, dtbos,
	   include_directories: incdirs,
	   dependencies: [ libfdt_dep ],
	   install: true)
