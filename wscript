import Options
import Environment
import sys, os, shutil, glob
from os import unlink, symlink, popen
from os.path import join, dirname, abspath, normpath

srcdir = '.'
blddir = 'build'
VERSION = '0.5.0'

def set_options(opt):
	opt.tool_options('compiler_cxx')
	opt.tool_options('compiler_cc')
	opt.tool_options('misc')
	
	opt.add_option( '--imlib2-includes'
		, action='store'
		, type='string'
		, default=False
		, help='Directory containing imlib2 header files'
		, dest='clearsilver_includes'
		)
	
	opt.add_option( '--imlib2'
		, action='store'
		, type='string'
		, default=False
		, help='Link to a shared imlib2 libraries'
		, dest='clearsilver'
		)

def configure(conf):
	conf.check_tool('compiler_cxx')
	if not conf.env.CXX: conf.fatal('c++ compiler not found')
	conf.check_tool('compiler_cc')
	if not conf.env.CC: conf.fatal('c compiler not found')
	conf.check_tool('node_addon')
	
	o = Options.options
	
	if o.clearsilver_includes:
	    conf.env.append_value("CPPFLAGS", '-I%s' % o.imlib2_includes)
	
	if o.clearsilver:
	    conf.env.append_value("LINKFLAGS", '-L%s' % o.imlib2)
	
	# print conf.env
	
	# check libs
	conf.check_cc( lib='imlib2', mandatory=True )

def build(bld):
	# print 'build'
	t = bld.new_task_gen('cxx', 'shlib', 'node_addon')
	t.target = 'Imlib2'
	t.source = './src/Imlib2.cc'
	t.includes = ['.']
	t.lib = ['imlib2']

def shutdown(ctx):
	pass
