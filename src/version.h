// Copyright (c) 2012-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERSION_H
#define BITCOIN_VERSION_H

/**
 * network protocol versioning
 */

static const int PROTOCOL_VERSION = 70016;

//! peercoin: used to communicate with clients that don't know how to send PoS information in headers
static const int OLD_VERSION = 70015;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 209;

//! disconnect from peers older than this proto version.
//! TrollCoin's live network runs protocol 60030 (legacy MIN_PEER_PROTO_VERSION=60020). The drop-in
//! must accept those peers to join the existing chain; without this it rejected every legacy node
//! ("using obsolete version 60030; disconnecting"). New<->new peers still negotiate 70016, and Core
//! gates wtxidrelay/sendaddrv2 on greatest_common_version>=70016 so old peers are never sent them.
static const int MIN_PEER_PROTO_VERSION = 60020;

//! BIP 0031, pong message, is enabled for all versions AFTER this one
static const int BIP0031_VERSION = 60000;

//! "sendheaders" command and announcing blocks with headers starts with this version
static const int SENDHEADERS_VERSION = 70012;

//! "feefilter" tells peers to filter invs to you by fee starts with this version
static const int FEEFILTER_VERSION = 70013;

//! short-id-based block download starts with this version
static const int SHORT_IDS_BLOCKS_VERSION = 70014;

//! not banning for invalid compact blocks starts with this version
static const int INVALID_CB_NO_BAN_VERSION = 70015;

//! "wtxidrelay" command for wtxid-based relay starts with this version
static const int WTXID_RELAY_VERSION = 70016;

#endif // BITCOIN_VERSION_H
