// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/device_cloud_policy_store_chromeos.h"

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/sequenced_task_runner.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/policy/device_policy_decoder_chromeos.h"
#include "chrome/browser/chromeos/policy/proto/chrome_device_policy.pb.h"
#include "chrome/browser/chromeos/settings/install_attributes.h"
#include "components/ownership/owner_key_util.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/proto/device_management_backend.pb.h"

namespace em = enterprise_management;

namespace {
const char kDMTokenCheckHistogram[] = "Enterprise.EnrolledPolicyHasDMToken";
}

namespace policy {

DeviceCloudPolicyStoreChromeOS::DeviceCloudPolicyStoreChromeOS(
    chromeos::DeviceSettingsService* device_settings_service,
    chromeos::InstallAttributes* install_attributes,
    scoped_refptr<base::SequencedTaskRunner> background_task_runner)
    : device_settings_service_(device_settings_service),
      install_attributes_(install_attributes),
      background_task_runner_(background_task_runner),
      weak_factory_(this) {
  device_settings_service_->AddObserver(this);
}

DeviceCloudPolicyStoreChromeOS::~DeviceCloudPolicyStoreChromeOS() {
  device_settings_service_->RemoveObserver(this);
}

void DeviceCloudPolicyStoreChromeOS::Store(
    const em::PolicyFetchResponse& policy) {
  // Cancel all pending requests.
  weak_factory_.InvalidateWeakPtrs();

  scoped_refptr<ownership::PublicKey> public_key(
      device_settings_service_->GetPublicKey());
  if (!install_attributes_->IsEnterpriseDevice() ||
      !device_settings_service_->policy_data() || !public_key.get() ||
      !public_key->is_loaded()) {
    status_ = STATUS_BAD_STATE;
    NotifyStoreError();
    return;
  }

  std::unique_ptr<DeviceCloudPolicyValidator> validator(
      CreateValidator(policy));
  validator->ValidateSignature(public_key->as_string(),
                               GetPolicyVerificationKey(),
                               install_attributes_->GetDomain(),
                               true);
  validator->ValidateAgainstCurrentPolicy(
      device_settings_service_->policy_data(),
      CloudPolicyValidatorBase::TIMESTAMP_FULLY_VALIDATED,
      CloudPolicyValidatorBase::DM_TOKEN_REQUIRED,
      CloudPolicyValidatorBase::DEVICE_ID_REQUIRED);
  validator.release()->StartValidation(
      base::Bind(&DeviceCloudPolicyStoreChromeOS::OnPolicyToStoreValidated,
                 weak_factory_.GetWeakPtr()));
}

void DeviceCloudPolicyStoreChromeOS::Load() {
  device_settings_service_->Load();
}

void DeviceCloudPolicyStoreChromeOS::InstallInitialPolicy(
    const em::PolicyFetchResponse& policy) {
  // Cancel all pending requests.
  weak_factory_.InvalidateWeakPtrs();

  if (!install_attributes_->IsEnterpriseDevice()) {
    status_ = STATUS_BAD_STATE;
    NotifyStoreError();
    return;
  }

  std::unique_ptr<DeviceCloudPolicyValidator> validator(
      CreateValidator(policy));
  validator->ValidateInitialKey(GetPolicyVerificationKey(),
                                install_attributes_->GetDomain());
  validator.release()->StartValidation(
      base::Bind(&DeviceCloudPolicyStoreChromeOS::OnPolicyToStoreValidated,
                 weak_factory_.GetWeakPtr()));
}

void DeviceCloudPolicyStoreChromeOS::DeviceSettingsUpdated() {
  if (!weak_factory_.HasWeakPtrs())
    UpdateFromService();
}

void DeviceCloudPolicyStoreChromeOS::OnDeviceSettingsServiceShutdown() {
  device_settings_service_ = nullptr;
}

std::unique_ptr<DeviceCloudPolicyValidator>
DeviceCloudPolicyStoreChromeOS::CreateValidator(
    const em::PolicyFetchResponse& policy) {
  std::unique_ptr<DeviceCloudPolicyValidator> validator(
      DeviceCloudPolicyValidator::Create(
          std::unique_ptr<em::PolicyFetchResponse>(
              new em::PolicyFetchResponse(policy)),
          background_task_runner_));
  validator->ValidateDomain(install_attributes_->GetDomain());
  validator->ValidatePolicyType(dm_protocol::kChromeDevicePolicyType);
  validator->ValidatePayload();
  return validator;
}

void DeviceCloudPolicyStoreChromeOS::OnPolicyToStoreValidated(
    DeviceCloudPolicyValidator* validator) {
  if (!validator->success()) {
    status_ = STATUS_VALIDATION_ERROR;
    validation_status_ = validator->status();
    NotifyStoreError();
    return;
  }

  device_settings_service_->Store(
      std::move(validator->policy()),
      base::Bind(&DeviceCloudPolicyStoreChromeOS::OnPolicyStored,
                 weak_factory_.GetWeakPtr()));
}

void DeviceCloudPolicyStoreChromeOS::OnPolicyStored() {
  UpdateFromService();
}

void DeviceCloudPolicyStoreChromeOS::UpdateFromService() {
  if (!install_attributes_->IsEnterpriseDevice()) {
    status_ = STATUS_BAD_STATE;
    NotifyStoreError();
    return;
  }

  CheckDMToken();
  UpdateStatusFromService();

  const chromeos::DeviceSettingsService::Status service_status =
      device_settings_service_->status();
  if (service_status == chromeos::DeviceSettingsService::STORE_SUCCESS) {
    policy_ = base::MakeUnique<em::PolicyData>();
    const em::PolicyData* policy_data = device_settings_service_->policy_data();
    if (policy_data)
      policy_->MergeFrom(*policy_data);

    PolicyMap new_policy_map;
    if (is_managed()) {
      DecodeDevicePolicy(*device_settings_service_->device_settings(),
                         &new_policy_map);
    }
    policy_map_.Swap(&new_policy_map);

    NotifyStoreLoaded();
    return;
  }
  NotifyStoreError();
}

void DeviceCloudPolicyStoreChromeOS::UpdateStatusFromService() {
  switch (device_settings_service_->status()) {
    case chromeos::DeviceSettingsService::STORE_SUCCESS:
      status_ = STATUS_OK;
      return;
    case chromeos::DeviceSettingsService::STORE_KEY_UNAVAILABLE:
      status_ = STATUS_BAD_STATE;
      return;
    case chromeos::DeviceSettingsService::STORE_POLICY_ERROR:
    case chromeos::DeviceSettingsService::STORE_OPERATION_FAILED:
      status_ = STATUS_STORE_ERROR;
      return;
    case chromeos::DeviceSettingsService::STORE_NO_POLICY:
    case chromeos::DeviceSettingsService::STORE_INVALID_POLICY:
    case chromeos::DeviceSettingsService::STORE_VALIDATION_ERROR:
    case chromeos::DeviceSettingsService::STORE_TEMP_VALIDATION_ERROR:
      status_ = STATUS_LOAD_ERROR;
      return;
  }
  NOTREACHED();
}

void DeviceCloudPolicyStoreChromeOS::CheckDMToken() {
  const chromeos::DeviceSettingsService::Status service_status =
      device_settings_service_->status();
  switch (service_status) {
    case chromeos::DeviceSettingsService::STORE_SUCCESS:
    case chromeos::DeviceSettingsService::STORE_KEY_UNAVAILABLE:
    case chromeos::DeviceSettingsService::STORE_NO_POLICY:
    case chromeos::DeviceSettingsService::STORE_INVALID_POLICY:
    case chromeos::DeviceSettingsService::STORE_VALIDATION_ERROR:
      // Continue with the check below.
      break;
    case chromeos::DeviceSettingsService::STORE_POLICY_ERROR:
    case chromeos::DeviceSettingsService::STORE_OPERATION_FAILED:
    case chromeos::DeviceSettingsService::STORE_TEMP_VALIDATION_ERROR:
      // Don't check for write errors or transient read errors.
      return;
  }

  if (dm_token_checked_) {
    return;
  }
  dm_token_checked_ = true;

  // At the time LoginDisplayHostImpl decides whether enrollment flow is to be
  // started, policy hasn't been read yet.  To work around this, once the need
  // for recovery is detected upon policy load, a flag is stored in prefs which
  // is accessed by LoginDisplayHostImpl early during (next) boot.
  const em::PolicyData* policy_data = device_settings_service_->policy_data();
  if (service_status == chromeos::DeviceSettingsService::STORE_SUCCESS &&
      policy_data && policy_data->has_request_token()) {
    UMA_HISTOGRAM_BOOLEAN(kDMTokenCheckHistogram, true);
  } else {
    LOG(ERROR) << "Device policy read on enrolled device yields "
               << "no DM token! Status: " << service_status << ".";
    chromeos::StartupUtils::MarkEnrollmentRecoveryRequired();
    UMA_HISTOGRAM_BOOLEAN(kDMTokenCheckHistogram, false);
  }
}

}  // namespace policy
