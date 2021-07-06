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
	'sfc.c',
	'shell.c',
	'sio.c',
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
if fs.is_dir(f'arch/@host@')
	incdirs += [ f'arch/@host@' ]
endif

if fs.is_file(f'arch/@host@/lpc.c')
	src += [ f'arch/@host@/lpc.c' ]
	conf_data.set10('have_lpc', 1)
else
	conf_data.set10('have_lpc', 0)
endif

configure_file(input: 'config.h.in',
	       output: 'config.h',
	       configuration: conf_data)

executable('doit', src, include_directories : incdirs)
