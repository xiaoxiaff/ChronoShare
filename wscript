# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

VERSION='0.6.0'
APPNAME='ChronoShare'

from waflib import Build, Logs, Utils, Task, TaskGen, Configure

def options(opt):
    opt.load(['compiler_cxx', 'qt4', 'gnu_dirs'])
    opt.load(['default-compiler-flags', 'osx-frameworks', 'boost', 'sqlite3', 'protoc', 'tinyxml',
              'doxygen', 'sphinx_build'],
             tooldir=['.waf-tools'])

    opt = opt.add_option_group('ChronoShare Options')

    opt.add_option('--with-tests', action='store_true', default=False, dest='with_tests',
                   help='''build unit tests''')
    opt.add_option('--with-log4cxx', action='store_true', default=False, dest='log4cxx',
                   help='''Compile with log4cxx logging support''')

    opt.add_option('--without-sqlite-locking', action='store_false', default=True,
                   dest='with_sqlite_locking',
                   help='''Disable filesystem locking in sqlite3 database '''
                        '''(use unix-dot locking mechanism instead). '''
                        '''This option may be necessary if home directory is hosted on NFS.''')

    if Utils.unversioned_sys_platform() == "darwin":
        opt.add_option('--with-auto-update', action='store_true', default=False, dest='autoupdate',
                       help='''(OSX) Download sparkle framework and enable autoupdate feature''')

def configure(conf):
    conf.load(['compiler_cxx', 'gnu_dirs'])
    conf.load(['default-compiler-flags', 'osx-frameworks', 'boost', 'sqlite3', 'tinyxml', 'qt4', 'protoc',
                'doxygen', 'sphinx_build'])

    conf.check_cfg(package='libndn-cxx', args=['--cflags', '--libs'], uselib_store='NDN_CXX')

    conf.check_sqlite3(mandatory=True)
    if not conf.options.with_sqlite_locking:
        conf.define('DISABLE_SQLITE3_FS_LOCKING', 1)

    conf.check_tinyxml(path=conf.options.tinyxml_dir)

    conf.define("CHRONOSHARE_VERSION", VERSION)
    conf.define("TRAY_ICON", "chronoshare-big.png")
    if Utils.unversioned_sys_platform() == "linux":
        conf.define("TRAY_ICON", "chronoshare-ubuntu.png")

    if conf.options.log4cxx:
        conf.check_cfg(package='liblog4cxx', args=['--cflags', '--libs'], uselib_store='LOG4CXX', mandatory=True)
        conf.define ("HAVE_LOG4CXX", 1)

    boost_libs = 'system random thread filesystem'
    if conf.options.with_tests:
        conf.env['TEST'] = 1
        conf.define('TEST', 1);
        boost_libs += ' unit_test_framework'

    conf.check_boost(lib=boost_libs)
    if conf.env.BOOST_VERSION_NUMBER < 104800:
        Logs.error("Minimum required boost version is 1.48.0")
        Logs.error("Please upgrade your distribution or install custom boost libraries" +
                   " (http://redmine.named-data.net/projects/nfd/wiki/Boost_FAQ)")
        return

    conf.define('SYSCONFDIR', conf.env['SYSCONFDIR'])

    conf.write_config_header('chronoshare-config.hpp')

def build (bld):
    bld(name='core-objects',
        target='core-objects',
        features=['cxx'],
        src='core/*.cpp',
        use=['LOG4CXX', 'BOOST'],
        includes='core',
        export_includes='. core',
        )
    
    adhoc = bld(
        name='adhoc',
        target='adhoc',
        features=['cxx'],
        use=['core-objects'],
    )

    if Utils.unversioned_sys_platform() == "darwin":
        adhoc.mac_app = True
        adhoc.source = 'adhoc/adhoc-osx.mm'
        adhoc.use += ['OSX_FOUNDATION', 'OSX_COREWLAN']

    chornoshare = bld(
        target="chronoshare",
        features=['cxx'],
        source = bld.path.ant_glob(['src/**/*.cpp', 'src/**/*.cc', 'src/**/*.proto']),
        use = "core adhoc NDN_CXX TINYXML",
        includes = "src",
        export_includes = "src",
        )

#     fs_watcher = bld (
#         target = "fs_watcher",
#         features = "qt4 cxx",
#         defines = "WAF=1",
#         source = bld.path.ant_glob(['fs-watcher/*.cpp']),
#         use = "SQLITE3 LOG4CXX scheduler executor QTCORE",
#         includes = "fs-watcher scheduler executor src",
#         )

#     http_server = bld(
#           target="http_server",
#           features="qt4 cxx",
#           source=bld.path.ant_glob(['server/*.cpp']),
#           includes="server src .",
#           use="BOOST QTCORE")

#     qt = bld(
#         target="ChronoShare",
#         features="qt4 cxx cxxprogram html_resources",
#         defines="WAF=1",
#         source=bld.path.ant_glob(['gui/*.cpp', 'gui/*.cpp', 'gui/images.qrc']),
#         includes="scheduler executor fs-watcher gui src adhoc server . ",
#         use="BOOST SQLITE3 QTCORE QTGUI LOG4CXX fs_watcher NDN_CXX database chronoshare http_server TINYXML",

#         html_resources = bld.path.find_dir("gui/html").ant_glob([
#                 '**/*.js', '**/*.png', '**/*.css',
#                 '**/*.html', '**/*.gif', '**/*.ico'
#                 ]),
#         )

#     if Utils.unversioned_sys_platform() == "darwin":
#         app_plist = '''<?xml version="1.0" encoding="UTF-8"?>
# <!DOCTYPE plist SYSTEM "file://localhost/System/Library/DTDs/PropertyList.dtd">
# <plist version="0.9">
# <dict>
#     <key>CFBundlePackageType</key>
#     <string>APPL</string>
#     <key>CFBundleIconFile</key>
#     <string>chronoshare.icns</string>
#     <key>CFBundleGetInfoString</key>
#     <string>Created by Waf</string>
#     <key>CFBundleIdentifier</key>
#     <string>edu.ucla.cs.irl.Chronoshare</string>
#     <key>CFBundleSignature</key>
#     <string>????</string>
#     <key>NOTE</key>
#     <string>THIS IS A GENERATED FILE, DO NOT MODIFY</string>
#     <key>CFBundleExecutable</key>
#     <string>%s</string>
#     <key>LSUIElement</key>
#     <string>1</string>
#     <key>SUPublicDSAKeyFile</key>
#     <string>dsa_pub.pem</string>
#     <key>CFBundleIconFile</key>
#     <string>chronoshare.icns</string>
# </dict>
# </plist>'''
#         qt.mac_app = "ChronoShare.app"
#         qt.mac_plist = app_plist % "ChronoShare"
#         qt.mac_resources = 'chronoshare.icns'
#         qt.use += " OSX_FOUNDATION OSX_COREWLAN adhoc"

#         if bld.env['HAVE_SPARKLE']:
#             qt.use += " OSX_SPARKLE"
#             qt.source += ["osx/auto-update/sparkle-auto-update.mm"]
#             qt.includes += " osx/auto-update"
#             if bld.env['HAVE_LOCAL_SPARKLE']:
#                 qt.mac_frameworks = "osx/Frameworks/Sparkle.framework"

#     if Utils.unversioned_sys_platform () == "linux":
#         bld(
#             features = "process_in",
#             target = "ChronoShare.desktop",
#             source = "ChronoShare.desktop.in",
#             install_prefix = "${DATADIR}/applications",
#             )
#         bld.install_files ("${DATADIR}/applications", "ChronoShare.desktop")
#         bld.install_files ("${DATADIR}/ChronoShare", "gui/images/chronoshare-big.png")

#     cmdline = bld (
#         target = "csd",
#         features = "qt4 cxx cxxprogram",
#         defines = "WAF",
#         source = "cmd/csd.cpp",
#         includes = "scheduler executor gui fs-watcher src . ",
#         use = "BOOSTSQLITE3 QTCORE QTGUI LOG4CXX fs_watcher NDN_CXX chronoshare TINYXML"
#         )

#     dump_db = bld (
#         target = "dump-db",
#         features = "cxx cxxprogram",
#         source = "cmd/dump-db.cpp",
#         includes = "scheduler executor gui fs-watcher src . ",
#         use = "BOOST SQLITE3 QTCORE LOG4CXX fs_watcher NDN_CXX chronoshare TINYXML"
#         )

#    bld.recurse('tests');

from waflib import TaskGen
@TaskGen.extension('.mm')
def m_hook(self, node):
    """Alias .mm files to be compiled the same as .cpp files, gcc/clang will do the right thing."""
    return self.create_compiled_task('cxx', node)

@TaskGen.extension('.js', '.png', '.css', '.html', '.gif', '.ico', '.in')
def sig_hook(self, node):
    node.sig = Utils.h_file(node.abspath())

@TaskGen.feature('process_in')
@TaskGen.after_method('process_source')
def create_process_in(self):
    dst = self.bld.path.find_or_declare(self.target)
    tsk = self.create_task('process_in', self.source, dst)

class process_in(Task.Task):
    color = 'PINK'

    def run(self):
        self.outputs[0].write(Utils.subst_vars(self.inputs[0].read(), self.env))

@TaskGen.feature('html_resources')
@TaskGen.before_method('process_source')
def create_qrc_task(self):
    output = self.bld.path.find_or_declare("gui/html.qrc")
    tsk = self.create_task('html_resources', self.html_resources, output)
    tsk.base_path = output.parent.get_src()
    self.create_rcc_task(output.get_src())

class html_resources(Task.Task):
    color='PINK'

    def __str__ (self):
        return "%s: Generating %s\n" % (self.__class__.__name__.replace('_task',''),
                                        self.outputs[0].path_from(self.outputs[0].ctx.launch_node()))

    def run (self):
        out = self.outputs[0]
        bld_out = out.get_bld ()
        src_out = out.get_src ()
        bld_out.write('<RCC>\n    <qresource prefix="/">\n')
        for f in self.inputs:
            bld_out.write ('        <file>%s</file>\n' % f.path_from (self.base_path), 'a')
        bld_out.write('    </qresource>\n</RCC>', 'a')

        src_out.write (bld_out.read(), 'w')
        return 0
