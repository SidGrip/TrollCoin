Sample init scripts and service configuration for trollcoind
==========================================================

Sample scripts and configuration files for systemd, Upstart and OpenRC
can be found in the contrib/init folder.

    contrib/init/trollcoind.service:    systemd service unit configuration
    contrib/init/trollcoind.openrc:     OpenRC compatible SysV style init script
    contrib/init/trollcoind.openrcconf: OpenRC conf.d file
    contrib/init/trollcoind.conf:       Upstart service configuration file
    contrib/init/trollcoind.init:       CentOS compatible SysV style init script
    contrib/init/org.trollcoin.trollcoind.plist: macOS launchd service configuration

Service User
---------------------------------

The systemd, OpenRC and Upstart configurations assume the existence of a
"trollcoin" user and group.  They must be created before attempting to use
these scripts.
The macOS configuration assumes trollcoind will be set up for the current user.

Configuration
---------------------------------

Running trollcoind as a daemon does not require any manual configuration. You may
set the `rpcauth` setting in the `trollcoin.conf` configuration file to override
the default behaviour of using a special cookie for authentication.

This password does not have to be remembered or typed as it is mostly used
as a fixed token that trollcoind and client programs read from the configuration
file, however it is recommended that a strong and secure password be used
as this password is security critical to securing the wallet should the
wallet be enabled.

If trollcoind is run with the "-server" flag (set by default), and no rpcpassword is set,
it will use a special cookie file for authentication. The cookie is generated with random
content when the daemon starts, and deleted when it exits. Read access to this file
controls who can access it through RPC.

By default the cookie is stored in the data directory, but its location can be overridden
with the option '-rpccookiefile'.

This allows for running trollcoind without having to do any manual configuration.

`conf` and `pid` accept relative paths which are interpreted as relative to
the data directory. `wallet` accepts absolute paths as well as paths relative
to the wallet directory (`walletdir`, inside the data directory by default).

For an example configuration file that describes the configuration settings,
see `share/examples/trollcoin.conf`.

Paths
---------------------------------

### Linux

These configurations assume several paths that might need to be adjusted.

    Binary:              /usr/bin/trollcoind
    Configuration file:  /etc/trollcoin/trollcoin.conf
    Data directory:      /var/lib/trollcoind
    PID file:            /var/run/trollcoind/trollcoind.pid (OpenRC and Upstart) or
                         /run/trollcoind/trollcoind.pid (systemd)
    Lock file:           /var/lock/subsys/trollcoind (CentOS)

The PID directory (if applicable) and data directory should both be owned by the
trollcoin user and group. It is advised for security reasons to make the
configuration file and data directory only readable by the trollcoin user and
group. Access to trollcoin-cli and other trollcoind rpc clients can then be
controlled by group membership.

NOTE: When using the systemd .service file, the creation of the aforementioned
directories and the setting of their permissions is automatically handled by
systemd. Directories are given a permission of 710, giving the trollcoin group
access to files under it _if_ the files themselves give permission to the
trollcoin group to do so. This does not allow
for the listing of files under the directory.

NOTE: It is not currently possible to override `datadir` in
`/etc/trollcoin/trollcoin.conf` with the current systemd, OpenRC, and Upstart init
files out-of-the-box. This is because the command line options specified in the
init files take precedence over the configurations in
`/etc/trollcoin/trollcoin.conf`. However, some init systems have their own
configuration mechanisms that would allow for overriding the command line
options specified in the init files (e.g. setting `BITCOIND_DATADIR` for
OpenRC).

### macOS

    Binary:              /usr/local/bin/trollcoind
    Configuration file:  ~/Library/Application Support/TrollCoin/trollcoin.conf
    Data directory:      ~/Library/Application Support/TrollCoin
    Lock file:           ~/Library/Application Support/TrollCoin/.lock

Installing Service Configuration
-----------------------------------

### systemd

Installing this .service file consists of just copying it to
/usr/lib/systemd/system directory, followed by the command
`systemctl daemon-reload` in order to update running systemd configuration.

To test, run `systemctl start trollcoind` and to enable for system startup run
`systemctl enable trollcoind`

NOTE: When installing for systemd in Debian/Ubuntu the .service file needs to be copied to the /lib/systemd/system directory instead.

### OpenRC

Rename trollcoind.openrc to trollcoind and drop it in /etc/init.d.  Double
check ownership and permissions and make it executable.  Test it with
`/etc/init.d/trollcoind start` and configure it to run on startup with
`rc-update add trollcoind`

### Upstart (for Debian/Ubuntu based distributions)

Upstart is the default init system for Debian/Ubuntu versions older than 15.04. If you are using version 15.04 or newer and haven't manually configured upstart you should follow the systemd instructions instead.

Drop trollcoind.conf in /etc/init.  Test by running `service trollcoind start`
it will automatically start on reboot.

NOTE: This script is incompatible with CentOS 5 and Amazon Linux 2014 as they
use old versions of Upstart and do not supply the start-stop-daemon utility.

### CentOS

Copy trollcoind.init to /etc/init.d/trollcoind. Test by running `service trollcoind start`.

Using this script, you can adjust the path and flags to the trollcoind program by
setting the BITCOIND_BIN and BITCOIND_OPTS environment variables in the file
/etc/sysconfig/trollcoind. You can also use the DAEMONOPTS environment variable here.

### macOS

Copy org.trollcoin.trollcoind.plist into ~/Library/LaunchAgents. Load the launch agent by
running `launchctl load ~/Library/LaunchAgents/org.trollcoin.trollcoind.plist`.

This Launch Agent will cause trollcoind to start whenever the user logs in.

NOTE: This approach is intended for those wanting to run trollcoind as the current user.
You will need to modify org.trollcoin.trollcoind.plist if you intend to use it as a
Launch Daemon with a dedicated trollcoin user.

Auto-respawn
-----------------------------------

Auto respawning is currently only configured for Upstart and systemd.
Reasonable defaults have been chosen but YMMV.
