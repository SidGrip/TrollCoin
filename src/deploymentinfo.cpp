// Copyright (c) 2016-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <deploymentinfo.h>

#include <consensus/params.h>

#include <string_view>

const struct VBDeploymentInfo VersionBitsDeploymentInfo[Consensus::MAX_VERSION_BITS_DEPLOYMENTS] = {
    {
        /*.name =*/ "testdummy",
        /*.gbt_force =*/ true,
    },
    // segwit + taproot are buried (height) deployments in TrollCoin, not versionbits.
};

std::string DeploymentName(Consensus::BuriedDeployment dep)
{
    assert(ValidDeployment(dep));
    switch (dep) {
    case Consensus::DEPLOYMENT_CSV:
        return "csv";
    case Consensus::DEPLOYMENT_DERSIG:
        return "bip66";
    case Consensus::DEPLOYMENT_SEGWIT:
        return "segwit";
    case Consensus::DEPLOYMENT_TAPROOT:
        return "taproot";
    } // no default case, so the compiler can warn about missing cases
    return "";
}

std::optional<Consensus::BuriedDeployment> GetBuriedDeployment(const std::string_view name)
{
    if (name == "segwit") {
        return Consensus::BuriedDeployment::DEPLOYMENT_SEGWIT;
    }
    if (name == "taproot") {
        return Consensus::BuriedDeployment::DEPLOYMENT_TAPROOT;
    }
    if (name == "bip66") {
        return Consensus::BuriedDeployment::DEPLOYMENT_DERSIG;
    }
    if (name == "csv") {
        return Consensus::BuriedDeployment::DEPLOYMENT_CSV;
    }
    return std::nullopt;
}
