#!/usr/bin/env ruby

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

#
# A tool for building patchfiles for Expat and pyexpat releases.
# The patchfiles built by this tool are easy to apply, but not
# very readable if one just wants to get an idea of what the changes
# contained in those patches are.
#
# USAGE:
# ./build-patches.rb [target_dir]
#
# Spaces in filenames are a big no-no.
#




def info(string)
  puts "[" + string + "]"
end

def sh(command)
  puts command
  system(command)
end

class Array
  def hjoin
    self.join('-')
  end
end

def make_patch(dir, basename, version)
  sfile = [basename, version, 'orig'].hjoin
  tfile = [basename, version, 'patched'].hjoin
  pfile = File.join(dir, ['patch', basename, version].hjoin)
  raise unless File.directory?(sfile)
  raise unless File.directory?(tfile)
  sh("diff -r -u -N --strip-trailing-cr #{sfile} #{tfile} > #{pfile}")
end







## Decide to where to build.
require 'tmpdir'
$build_home = ARGV[0] || File.join(Dir.tmpdir, 'pyexpat-build')
info "building in directory #{$build_home}"

## Ensure that the build directory does not exist.
require 'fileutils'
include FileUtils::Verbose
begin
  rm_r($build_home)
rescue Errno::ENOENT
  # does not exist, I suppose
end

## Do an "svn export".
sh("svn export . " + $build_home)

cd($build_home) do
  ## Create a directory for the build results.
  $dist_dir = File.join($build_home, 'dist')
  mkdir $dist_dir
  info "build results placed under " + $dist_dir

  ## Build all the patchfiles.
  ## Note that any documentation regarding the patches
  ## are contained in the patches themselves.
  list = Dir['*-*-patched']
  list.each do |pdir|
    if pdir =~ /(.*)-(.*)-patched/
      make_patch($dist_dir, $1, $2)
    else
      raise
    end
  end
end
