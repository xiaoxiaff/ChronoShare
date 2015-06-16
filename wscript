# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-
VERSION='1.0'
APPNAME='ChronoShare'

from waflib import Configure, Utils, Logs, Context
import os

def options(opt):

    opt.load(['compiler_c', 'compiler_cxx', 'qt4', 'gnu_dirs'])

    opt.load(['default-compiler-flags', 'boost', 'protoc',
              'doxygen', 'sphinx_build'],
              tooldir=['waf-tools'])

    opt = opt.add_option_group('ChronoShare Options')

    opt.add_option('--with-tests', action='store_true', default=False, dest='with_tests',
                   help='''build unit tests''')

    opt.add_option('--with-log4cxx', action='store_true', default=False, dest='log4cxx',
                   help='''Enable log4cxx''')

def configure(conf):
    conf.load(['compiler_c', 'compiler_cxx', 'qt4',
               'default-compiler-flags', 'boost', 'protoc', 'gnu_dirs',
               'doxygen', 'sphinx_build'])

    conf.check_cfg(package='libndn-cxx', args=['--cflags', '--libs'],
                   uselib_store='NDN_CXX', mandatory=True)

    conf.check_cfg(package='libevent', args=['--cflags', '--libs'], 
                   uselib_store='LIBEVENT', mandatory=True)

    conf.check_cfg(package='libevent_pthreads', args=['--cflags', '--libs'], 
                   uselib_store='LIBEVENT_PTHREADS', mandatory=True)

    if conf.options.log4cxx:
        conf.check_cfg(package='liblog4cxx', args=['--cflags', '--libs'],
                       uselib_store='LOG4CXX', mandatory=True)
        conf.define("HAVE_LOG4CXX", 1)

#    conf.check_cfg (package='ChronoSync', args=['ChronoSync >= 0.1', '--cflags', '--libs'],
#                    uselib_store='SYNC', mandatory=True)

    boost_libs = 'system test iostreams filesystem regex thread date_time'
    if conf.options.with_tests:
        conf.env['WITH_TESTS'] = 1
        conf.define('WITH_TESTS', 1);
        boost_libs += ' unit_test_framework'

    conf.check_boost(lib=boost_libs)
    if conf.env.BOOST_VERSION_NUMBER < 104800:
        Logs.error("Minimum required boost version is 1.48.0")
        Logs.error("Please upgrade your distribution or install custom boost libraries" +
                   " (http://redmine.named-data.net/projects/nfd/wiki/Boost_FAQ)")
        return

    if not conf.check_cfg(package='openssl', args=['--cflags', '--libs'], uselib_store='SSL', mandatory=False):
        libcrypto = conf.check_cc(lib='crypto',
                                  header_name='openssl/crypto.h',
                                  define_name='HAVE_SSL',
                                  uselib_store='SSL')
    else:
        conf.define ("HAVE_SSL", 1)
    if not conf.get_define ("HAVE_SSL"):
        conf.fatal ("Cannot find SSL libraries")

    conf.write_config_header('src/config.h')

def build (bld):
    feature_list = 'qt4 cxx'
    if bld.env["WITH_TESTS"]:
        feature_list += ' cxxstlib'
    else:
        feature_list += ' cxxprogram'

    executor = bld.objects (
        target = "executor",
        features = ["cxx"],
        source = bld.path.ant_glob(['executor/**/*.cc']),
        use = 'BOOST LIBEVENT LIBEVENT_PTHREADS LOG4CXX',
        includes = "executor src",
        )

    scheduler = bld.objects (
        target = "scheduler",
        features = ["cxx"],
        source = bld.path.ant_glob(['scheduler/**/*.cc']),
        use = 'BOOST LIBEVENT LIBEVENT_PTHREADS LOG4CXX executor',
        includes = "scheduler executor src",
        )
#    chornoshare = bld (
#        target="chronoshare",
#        features=['cxx'],
#        source = bld.path.ant_glob(['src/**/*.cc', 'src/**/*.cpp', 'src/**/*.proto']),
#        use = "BOOST BOOST_FILESYSTEM BOOST_DATE_TIME SQLITE3 LOG4CXX scheduler NDN_CXX",
#        includes = "ccnx scheduler src executor",
#        )

    chornoshare = bld (
        target="mysync",
        features=['cxx'],
        source = bld.path.ant_glob([
#                                    'src/dispatcher.cc', 'src/fetcher.cc', 'src/fetch-manager.cc', 'src/fetch-task-db.cc',
#                                    'src/state-server.cc', 'src/content-server.cc',
#                                    'src/object-db.cc', 'src/object-manager.cc',
                                    'src/action-log.cc', #'src/file-state.cc',
                                    'src/sync-core.cc', 
                                    'src/sync-log.cc', 
                                    'src/db-helper.cc', 'src/logging.cc', 'src/**/*.proto']),
        use = "BOOST SQLITE3 LOG4CXX scheduler NDN_CXX",
        includes = "scheduler src executor",
        )

    # Unit tests
    if bld.env["WITH_TESTS"]:
      unittests = bld.program (
          target="unit-tests",
          source = bld.path.ant_glob(['test/**/*.cpp']),
          features=['cxx', 'cxxprogram'],
          use = 'BOOST ChronoShare',
          includes = "src .",
          install_path = None,
          defines = 'TEST_CERT_PATH=\"%s/cert-test\"' %(bld.bldnode),
          )

    # Debug tools
    if bld.env["_DEBUG"]:
        for app in bld.path.ant_glob('debug-tools/*.cc'):
            bld(features=['cxx', 'cxxprogram'],
                target = '%s' % (str(app.change_ext('','.cc'))),
                source = app,
                use = 'NDN_CXX',
                includes = "src .",
                install_path = None,
            )
