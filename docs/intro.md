ChronoShare
===========

ChronoShare is a decentralized file sharing application based on a new communication primitive, ChronoSync, which builds on top of the Named Data Networking (NDN) and enables dataset synchronization among a group of participants in a completely distributed way.

It uses ChronoSync library to synchronize the operations to the shared-folder and levels NDN's advantage of natural multicast support. The sharing process is completely decentralized, but it is also very easy to add a permanent storage server.

The ChronoShare supplies various features, such as:

- Version controlled
- NDN-JS interface for versioning history browsing and checking out old version
- Dropbox like user experience (ok, their UI is fancier)

The next release version will also include NDN security management, group access control and read/right permission control.

## Download

- macOS 10.12

    * [Latest version](https://named-data.net/binaries/ChronoShare/ChronoShare.dmg)

- Source

    * [Github](https://github.com/named-data/ChronoShare)

- Issue requests and bug reporting

    * [NDN Redmine](https://redmine.named-data.net/projects/chronoshare/issues)

- [Release notes](https://github.com/named-data/ChronoShare/blob/master/RELEASE_NOTES.md#release-notes)

## Getting Started

Download `.dmg` of the latest version of the ChronoShare and install it by simply double clicking dmg package and dragging `ChronoShare.app` to Application folder.

## Install from source code

For Mac OS version earlier than 10.12, you can install the ChronoShare from the source code.

- prerequisite
	* ndn-cxx: https://named-data.net/doc/ndn-cxx/current/INSTALL.html
	* protobuf, qt5, tinyxml: $brew install protobuf qt5 tinyxml

- Download ChronoShare from source code
	* [Github] git clone https://github.com/named-data/ChronoShare

- then compiler the source code
./waf configure
./waf

- then go build directory and run ChronoShare
cd build
./ChronoShare

If the compiling has error that html.qrc doesn't exist, just retry ./waf until it compiles in the correct way :)

### Launch ChronoShare

In the Launchpad, click on ChronoShare application icon to launch ChronoShare. Application icon will show on the menu bar and the main menu can be opened from there.

If it's the first time setup, it will prompt you to input the username, sharefolder name and path to your sharefolder.

- Username is the only identifier of each devices.
- Sharefolder will be created under path to your sharefolder once sharefolder name is assigned.
- When path is selected, the sharefolder will be created under that certain path.

All settings can be changed at application menu at anytime.

### Operation

Whenever changes are made in local or remote devices, the sharefolder will be synchronized automatically. You can open the sharefolder by both Finder and application menu.

The ChronoShare also provides the Web UI where list all recent changes in sharefolder or in timeline sequence. The Web UI can be accessed through application menu.

