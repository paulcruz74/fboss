// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/qsfp_service/TransceiverValidator.h"

DEFINE_bool(
    enable_tcvr_validation_scuba_logging,
    false,
    "Enable scuba logging for transceiver validation feature in qsfp_service");

namespace facebook::fboss {

using ValidationResult = std::pair<bool, std::string>;

TransceiverValidator::TransceiverValidator(
    const std::vector<VendorConfig>& tcvrConfigs) {
  for (auto vendorConfig : tcvrConfigs) {
    tcvrValMap_[vendorConfig.vendorName().value()] =
        std::move(vendorConfig.partNumberToTransceiverAttributes().value());
  }
};

// TODO(smenta): Once firmware sync is enabled, allow invalid firmware matches
// to invalidate config.
ValidationResult TransceiverValidator::validateTcvrAndReason(
    const TransceiverValidationInfo& tcvrInfo) {
  // Making vendor name capitalized since we pass in a config that assumes
  // capitalization.
  std::string vendorName = tcvrInfo.vendorName;
  transform(vendorName.begin(), vendorName.end(), vendorName.begin(), toupper);

  auto vendorItr = tcvrValMap_.find(vendorName);
  if (vendorItr == tcvrValMap_.end()) {
    return ValidationResult(false, "invalidVendorName");
  }

  auto partNumItr = vendorItr->second.find(tcvrInfo.vendorPartNumber);
  if (partNumItr == tcvrValMap_[tcvrInfo.vendorName].end()) {
    return ValidationResult(false, "invalidVendorPartNumber");
  }

  auto& firmwareVersions =
      partNumItr->second.supportedFirmwareVersions().value();
  FirmwarePair fwPair;
  fwPair.applicationFirmwareVersion() = tcvrInfo.firmwareVersion;
  fwPair.dspFirmwareVersion() = tcvrInfo.dspFirmwareVersion;
  if (std::find(firmwareVersions.begin(), firmwareVersions.end(), fwPair) ==
      firmwareVersions.end()) {
    XLOG(INFO)
        << "[Transceiver Validation] Combination of firmware version and dsp firmware version is not valid. This will not affect overall config validity.";
  }

  auto& portProfiles = partNumItr->second.supportedPortProfiles().value();
  for (auto portProfileId : tcvrInfo.portProfileIds) {
    if (std::find(portProfiles.begin(), portProfiles.end(), portProfileId) ==
        portProfiles.end()) {
      return ValidationResult(false, "invalidPortProfileId");
    }
  }

  return ValidationResult(true, "");
};

bool TransceiverValidator::validateTcvr(
    const TransceiverValidationInfo& tcvrInfo,
    std::string& notValidatedReason) {
  ValidationResult validPair;

  if (!tcvrInfo.validEepromChecksums) {
    validPair = ValidationResult(false, "invalidEepromChecksums");
  } else if (!tcvrInfo.requiredFields.first) {
    validPair = ValidationResult(tcvrInfo.requiredFields);
  } else {
    validPair = validateTcvrAndReason(tcvrInfo);
  }

  std::vector<std::string> portProfileIdStrings;
  for (auto portProfileId : tcvrInfo.portProfileIds) {
    portProfileIdStrings.push_back(
        apache::thrift::util::enumNameSafe(portProfileId));
  }

  auto tcvrConfigStr = fmt::format(
      "(vendorName: {}, vendorPartNumber: {}, firmwareVersion: {}, dspFirmwareVersion: {}, portProfile: {})",
      tcvrInfo.vendorName,
      tcvrInfo.vendorPartNumber,
      tcvrInfo.firmwareVersion,
      tcvrInfo.dspFirmwareVersion,
      folly::join(",", portProfileIdStrings));

  if (validPair.first) {
    XLOGF(
        INFO,
        "Transceiver {} has a valid configuration {}.",
        tcvrInfo.id,
        tcvrConfigStr);
  } else {
    XLOGF(
        WARN,
        "Transceiver {} has an invalid configuration {} due to {}.",
        tcvrInfo.id,
        tcvrConfigStr,
        validPair.second);
  }

  notValidatedReason = validPair.second;
  return validPair.first;
};
} // namespace facebook::fboss