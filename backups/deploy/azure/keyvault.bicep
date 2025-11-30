// deploy/azure/keyvault.bicep
// Key Vault for storing secrets and certificates

targetScope = 'resourceGroup'

@description('Location for the Key Vault')
param location string = resourceGroup().location

@description('Key Vault name (3-24 chars, alphanumeric and hyphens)')
@minLength(3)
@maxLength(24)
param keyVaultName string

@description('Resource tags')
param tags object = {}

@description('Enable soft delete')
param enableSoftDelete bool = true

@description('Soft delete retention period in days')
@minValue(7)
@maxValue(90)
param softDeleteRetentionInDays int = 90

@description('Enable purge protection')
param enablePurgeProtection bool = false

@description('SKU name')
@allowed([
  'standard'
  'premium'
])
param skuName string = 'standard'

@description('Enable RBAC authorization (recommended over access policies)')
param enableRbacAuthorization bool = true

// ========================================
// Key Vault
// ========================================

resource keyVault 'Microsoft.KeyVault/vaults@2023-02-01' = {
  name: keyVaultName
  location: location
  tags: tags
  properties: {
    sku: {
      family: 'A'
      name: skuName
    }
    tenantId: subscription().tenantId
    enableSoftDelete: enableSoftDelete
    softDeleteRetentionInDays: softDeleteRetentionInDays
    enablePurgeProtection: enablePurgeProtection ? true : null
    enableRbacAuthorization: enableRbacAuthorization
    enabledForDeployment: false
    enabledForDiskEncryption: false
    enabledForTemplateDeployment: true
    publicNetworkAccess: 'Enabled'
    networkAcls: {
      defaultAction: 'Allow'
      bypass: 'AzureServices'
      ipRules: []
      virtualNetworkRules: []
    }
  }
}

// ========================================
// Outputs
// ========================================

@description('Key Vault name')
output keyVaultName string = keyVault.name

@description('Key Vault resource ID')
output keyVaultId string = keyVault.id

@description('Key Vault URI')
output keyVaultUri string = keyVault.properties.vaultUri
