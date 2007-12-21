#!/usr/bin/env ruby

#
# makefile.rb
#
# Copyright 2004 Helsinki Institute for Information Technology (HIIT)
# and the authors.  All rights reserved.
#
# Authors: Tero Hasu <tero.hasu@hut.fi>
#

# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

USAGE = '

 USAGE:
 ./makefile.rb [options] targets...

 Note that the order of the targets listed is significant.

 This file kind of implements "make" and a top-level makefile
 for this project. Some of the rules might only be executable
 in a Windows/Cygwin environment, some only in a genuine Unix
 environment, and some might execute in both.

'


COMP_NAME = "pyexpat"
COMP_UID = 0x10206ba2 # allocated from Symbian
MAJOR_VERSION = 1
MINOR_VERSION = 8
VERSION_STRING = '%d.%02d' % [MAJOR_VERSION, MINOR_VERSION]



require 'parsearg'
$USAGE = "print USAGE"
parseArgs(0, nil, 'd:pqst')
$tasks = ARGV

require 'rake048'
def fu_output_message(msg)
  # Print to standard output, dammit --
  # what does it take to override the default setting?
  @fileutils_output = $stdout
  super(msg)
end

verbose(!$OPT_q)
nowrite($OPT_s)
$print_pkg = $OPT_p

PROJECT_HOME = File.dirname(File.expand_path(__FILE__))

require 'build/makeutils'
require 'build/devices'

class Device
  def binaries_home
    File.join(PROJECT_HOME, "binaries", @name)
  end

  def pkg_name
    "pyexpat-%s.pkg" % @name
  end

  def pkg_file
    File.join("pyexpat-2.2.3-patched", pkg_name)
  end

  def shared_sis_file
    return nil unless EXPORT_DEVICE_SET.include?(@name)
    sharedname = "%s-%s.sis" % [COMP_NAME, @name]
    sharedfile = File.join(PROJECT_HOME, "..", "binaries", sharedname)
    return sharedfile
  end
end

BUILD_DEVICE_SET = ($OPT_d ? $OPT_d.split(/,/) : %w{series60_v12})
EXPORT_DEVICE_SET = ["series60_v12"] & BUILD_DEVICE_SET
DEVICES = Devices::get_named_info(BUILD_DEVICE_SET)



task :default => :build

task :devices do
  for device in Devices::get_all_info
    puts device.name
  end
end

# product_id:: platform UID
def write_pkg_file(file, product_id)
  content = '
; We could otherwise simply use a 0 value
; (as Python imports should be purely name-based),
; but SIS file prerequisites do require a UID,
; so we are using a UID allocated from Symbian.
#{"%s"}, (0x%08x), %d, %d, 0

;;;
;;; Series 60 v0.9
;;;
(0x101f6f88), 0, 0, 0, {"Series60ProductID"}

;;;
;;; Python for Series 60
;;;
(0x10201510), 0, 0, 0, {"Python for Series 60"}

;;;
;;; license
;;;
; "doc\COPYING" - "", FILETEXT, TEXTCONTINUE

;;;
;;; native extension (includes Expat)
;;;
"epoc32\release\armi\urel\pyexpat.pyd" -
"!:\system\libs\pyexpat.pyd"

;;;
;;; standard Python XML library
;;;
"python-lib\xml\dom\domreg.py" -
"!:\system\libs\xml\dom\domreg.py"
"python-lib\xml\dom\minidom.py" -
"!:\system\libs\xml\dom\minidom.py"
"python-lib\xml\dom\pulldom.py" -
"!:\system\libs\xml\dom\pulldom.py"
"python-lib\xml\dom\__init__.py" -
"!:\system\libs\xml\dom\__init__.py"
"python-lib\xml\parsers\expat.py" -
"!:\system\libs\xml\parsers\expat.py"
"python-lib\xml\parsers\__init__.py" -
"!:\system\libs\xml\parsers\__init__.py"
"python-lib\xml\sax\expatreader.py" -
"!:\system\libs\xml\sax\expatreader.py"
"python-lib\xml\sax\handler.py" -
"!:\system\libs\xml\sax\handler.py"
"python-lib\xml\sax\saxutils.py" -
"!:\system\libs\xml\sax\saxutils.py"
"python-lib\xml\sax\xmlreader.py" -
"!:\system\libs\xml\sax\xmlreader.py"
"python-lib\xml\sax\_exceptions.py" -
"!:\system\libs\xml\sax\_exceptions.py"
"python-lib\xml\sax\__init__.py" -
"!:\system\libs\xml\sax\__init__.py"
"python-lib\xml\__init__.py" -
"!:\system\libs\xml\__init__.py"
' % [COMP_NAME, COMP_UID, MAJOR_VERSION, MINOR_VERSION]

  write_file(file, content, true)
end

task :pkg do
  for device in DEVICES
    write_pkg_file(device.pkg_file, device.platform_uid)
  end
end

task :sis => :pkg do
  for device in DEVICES
    mkdir_p device.binaries_home

    basicname = "%s.sis" % COMP_NAME
    longname = "%s-%s-%s.sis" % [COMP_NAME, VERSION_STRING, device.name]
    longfile = File.join(device.binaries_home, longname)

    device.in_env do
      cd('pyexpat-2.2.3-patched', :noop => false) do
        er = device.dos_epocroot.gsub(/\\/, "\\\\\\\\")
        sh("makesis -v -d%s %s %s" %
            [er, device.pkg_name, basicname])
        cp basicname, longfile
      end
    end

    sharedfile = device.shared_sis_file
    if sharedfile
      ln(longfile, sharedfile, :force => true)
    end
  end
end

task :pyd do
  for device in DEVICES
    device.in_env do
      cd('pyexpat-2.2.3-patched', :noop => false) do
        sh("bldmake.bat bldfiles")
        unless device.name == "series60_v21"
          # MSVC6 building is broken in this release
          sh("makmake.bat %s.mmp vc6" % COMP_NAME)
          sh("./abld.bat -v build wins udeb")
        end
        sh("./abld.bat -v build armi urel")
      end
    end
  end
end

task :deploy do
  for device in DEVICES
    device.in_env do
      cd('pyexpat-2.2.3-patched', :noop => false) do
        sh("./deploy.bat")
      end
    end
  end
end

task :build => [:pyd, :deploy, :sis]


task :run do
  for device in DEVICES
    device.in_env do
      sh("epoc.bat")
    end
  end
end


task :cleanlog do
  for device in DEVICES
    logdir = File.join(device.epoc32_home, 'wins/c/logs/pyexpat')
    if File.exist?(logdir)
      rm Dir.glob(File.join(logdir, '*.txt'))
    end
  end
end

task :cleanpyd do
  for device in DEVICES
    rm Dir.globi(File.join(device.epoc32_home,
      'release/armi/urel/pyexpat.*')) |
    Dir.globi(File.join(device.epoc32_home,
      'release/wins/udeb/pyexpat.*')) |
    Dir.globi(File.join(device.epoc32_home,
      'release/wins/udeb/z/system/libs/pyexpat.*'))
  end
end

task :cleanbuild do
  for device in DEVICES
    may_fail do; rm_r [File.join(device.epoc32_home, 'build')]; end
  end
  may_fail do; rm 'ABLD.BAT'; end
  rm Dir.globi('*.dsp') | Dir.globi('*.dsw') |
    Dir.globi('*.sup.make') | Dir.globi('*.uid.cpp')
end

task :cleanbak do
  rm Dir.globi('**/*~')
end

task :clean => [:cleanlog, :cleanpyd, :cleanbuild, :cleanbak]




# returns 0 for no differences, 1 for differences,
# and 2 for trouble
def diff(command)
  command = "diff " + command
  puts command if verbose()
  system(command)
  raise "error #{$?}" unless [0, 1].include?($? >> 8)
end

def make_patch(dir, basename, version)
  sfile = [basename, version, 'orig'].join('-')
  tfile = [basename, version, 'patched'].join('-')
  pfile = File.join(dir, ['patch', basename, version].join('-'))
  raise unless File.directory?(sfile)
  raise unless File.directory?(tfile)
  diff("-r -u -N --strip-trailing-cr #{sfile} #{tfile} > #{pfile}")
  #diff("-r -u -N #{sfile} #{tfile} > #{pfile}")
end

DIST_HOME = File.join(PROJECT_HOME, 'pyexpat-symbian-patch')

task :dist do
  may_fail do; rm_r DIST_HOME; end
  mkdir DIST_HOME

  docs = ['pyexpat-2.2.3-patched/doc/COPYING',
    'README',
    'doc/readme-expat-1.95.8-patch.txt',
    'doc/readme-pyexpat-2.2.3-patch.txt']
  bins = []
  exes = []

  install docs + bins, DIST_HOME, :mode => 0644
  install exes, DIST_HOME, :mode => 0755

  require 'tmpdir'
  build_home = File.join(Dir::tmpdir, 'patch-build')
  may_fail do; rm_r build_home; end
  sh("svn export . " + build_home)
  cd(build_home) do
    ## pyexpat patch
    mv 'pyexpat-2.2.3-orig/src/pyexpat.c',
      'pyexpat-2.2.3-orig/src/pyexpat.cpp'
    rm_r 'pyexpat-2.2.3-patched/python-lib'
    make_patch(DIST_HOME, "pyexpat", "2.2.3")

    ## expat patch
    mv 'expat-1.95.8-orig/lib/xmltok_impl.c',
      'expat-1.95.8-orig/lib/xmltok_impl.inl'
    mv 'expat-1.95.8-orig/lib/xmltok_ns.c',
      'expat-1.95.8-orig/lib/xmltok_ns.inl'
    make_patch(DIST_HOME, "expat", "1.95.8")
  end

  ## pyexpat patch applying script
  write_script(File.join(DIST_HOME,
    "construct-pyexpat-source.sh")) do |output|
    output.puts <<EOF
#!/bin/bash
# Even if don't have the bash shell, this file should give you an idea
# of how to construct full, patched source for pyexpat.
wget http://www.python.org/ftp/python/2.2.3/Python-2.2.3.tgz
tar xzvf Python-2.2.3.tgz Python-2.2.3/Modules Python-2.2.3/Lib/xml
mkdir -p pyexpat-2.2.3-patched/python-lib
mkdir -p pyexpat-2.2.3-patched/src
mv Python-2.2.3/Modules/pyexpat.c pyexpat-2.2.3-patched/src/pyexpat.cpp
patch -p1 -d pyexpat-2.2.3-patched < patch-pyexpat-2.2.3
mv Python-2.2.3/Lib/xml pyexpat-2.2.3-patched/python-lib/
rm -r Python-2.2.3
EOF
  end

  ## expat patch applying script
  write_script(File.join(DIST_HOME,
    "construct-expat-source.sh")) do |output|
    output.puts <<EOF
#!/bin/bash
# Even if don't have the bash shell, this file should give you an idea
# of how to construct full, patched source for Expat.
wget http://switch.dl.sourceforge.net/sourceforge/expat/expat-1.95.8.tar.gz
tar xzvf expat-1.95.8.tar.gz expat-1.95.8
mv expat-1.95.8 expat-1.95.8-patched
mv expat-1.95.8-patched/lib/xmltok_impl.c expat-1.95.8-patched/lib/xmltok_impl.inl
mv expat-1.95.8-patched/lib/xmltok_ns.c expat-1.95.8-patched/lib/xmltok_ns.inl
patch -p1 -d expat-1.95.8-patched < patch-expat-1.95.8
EOF
  end
end


task :web do
  trunk_home = File.dirname(File.dirname(File.dirname(PROJECT_HOME)))
  web_home = File.join(trunk_home, 'html', 'pdis', 'download', 'pyexpat')

  if File.exist?('/usr/bin/tidy')
    index_file = File.join(web_home, 'index.html')
    sh("tidy -e " + index_file)
  end

  cp('pyexpat-2.2.3-patched/doc/COPYING', web_home + '/')

  for device in DEVICES
    sisfile = device.shared_sis_file
    if sisfile
      sisname = [COMP_NAME, VERSION_STRING, device.name].join('-') + ".sis"
      cp(sisfile, File.join(web_home, sisname))
    end
  end

  tarfile = File.join(web_home,
                      "pyexpat-symbian-patch-%s.tar.gz" % VERSION_STRING)
  sh("tar cvzf #{tarfile} #{File.basename(DIST_HOME)}")
end



$tasks = [:default] if $tasks.empty?
Dir.chdir(PROJECT_HOME) do
  $tasks.each do |task_name|
    Task[task_name].invoke
  end
end
