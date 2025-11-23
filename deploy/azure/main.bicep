// deploy/azure/main.bicep
// Root Bicep template for Protogate Core Server deployment
// Deploys Container Apps, Key Vault, DNS Zone, Log Analytics

targetScope = 'resourceGroup'

@description('Location for all resources')
param location string = resourceGroup().location

@description('Environment name (dev, staging, prod)')
@allowed([
  'dev'
  'staging'
  'prod'
])
param environment string = 'dev'

@description('Base name for resources (e.g., protogate)')
@minLength(3)
@maxLength(15)
param baseName string = 'protogate'

@description('DNS domain name (e.g., tunnel.example.com)')
param dnsDomainName string

@description('Container image tag')
param imageTag string = 'latest'

@description('Minimum number of replicas')
@minValue(0)
@maxValue(10)
param minReplicas int = 1

@description('Maximum number of replicas')
@minValue(1)
@maxValue(30)
param maxReplicas int = 10

@description('CPU cores per replica')
param cpu string = '0.5'

@description('Memory per replica (e.g., 1Gi)')
param memory string = '1Gi'

@description('Timestamp for resource tagging (defaults to current UTC time)')
param timestamp string = utcNow('yyyy-MM-dd')

// ========================================
// Variables
// ========================================

var resourcePrefix = '${baseName}-${environment}'
var containerAppName = '${resourcePrefix}-app'
var keyVaultName = '${baseName}${environment}kv' // No hyphens, max 24 chars
var logAnalyticsName = '${resourcePrefix}-logs'
var containerRegistryName = '${baseName}${environment}acr' // No hyphens

// Tags
var commonTags = {
  Environment: environment
  Project: 'Protogate'
  ManagedBy: 'Bicep'
  CreatedDate: timestamp
}

// ========================================
// Module: Log Analytics Workspace
// ========================================

module logAnalytics 'log-analytics.bicep' = {
  name: '${deployment().name}-logs'
  params: {
    location: location
    logAnalyticsName: logAnalyticsName
    tags: commonTags
  }
}

// ========================================
// Module: Key Vault
// ========================================

module keyVault 'keyvault.bicep' = {
  name: '${deployment().name}-keyvault'
  params: {
    location: location
    keyVaultName: keyVaultName
    tags: commonTags
    enableSoftDelete: true
    softDeleteRetentionInDays: environment == 'prod' ? 90 : 7
    enablePurgeProtection: environment == 'prod'
  }
}

// ========================================
// Module: DNS Zone
// ========================================

module dnsZone 'dns-zone.bicep' = {
  name: '${deployment().name}-dns'
  params: {
    dnsDomainName: dnsDomainName
    tags: commonTags
  }
}

// ========================================
// Module: Container App
// ========================================

module containerApp 'container-app.bicep' = {
  name: '${deployment().name}-container-app'
  params: {
    location: location
    containerAppName: containerAppName
    environmentName: '${resourcePrefix}-env'
    logAnalyticsWorkspaceId: logAnalytics.outputs.workspaceId
    logAnalyticsWorkspaceKey: logAnalytics.outputs.workspaceKey
    keyVaultUri: keyVault.outputs.keyVaultUri
    containerImage: '${containerRegistryName}.azurecr.io/protogate:${imageTag}'
    containerRegistryServer: '${containerRegistryName}.azurecr.io'
    minReplicas: minReplicas
    maxReplicas: maxReplicas
    cpu: cpu
    memory: memory
    tags: commonTags
  }
}

// ========================================
// Update DNS with Container App FQDN
// ========================================

module dnsUpdate 'dns-record.bicep' = {
  name: '${deployment().name}-dns-update'
  params: {
    dnsZoneName: dnsZone.outputs.dnsZoneName
    containerAppFqdn: containerApp.outputs.fqdn
  }
}

// ========================================
// Outputs
// ========================================

@description('Container App FQDN')
output containerAppFqdn string = containerApp.outputs.fqdn

@description('Container App name')
output containerAppName string = containerApp.outputs.name

@description('Key Vault name')
output keyVaultName string = keyVault.outputs.keyVaultName

@description('Key Vault URI')
output keyVaultUri string = keyVault.outputs.keyVaultUri

@description('Log Analytics Workspace ID')
output logAnalyticsWorkspaceId string = logAnalytics.outputs.workspaceId

@description('DNS Zone name')
output dnsZoneName string = dnsZone.outputs.dnsZoneName

@description('DNS Zone Name Servers')
output dnsNameServers array = dnsZone.outputs.nameServers

@description('Container App Managed Identity Principal ID')
output managedIdentityPrincipalId string = containerApp.outputs.managedIdentityPrincipalId

@description('Container App URL (HTTPS)')
output containerAppUrl string = 'https://${containerApp.outputs.fqdn}'

@description('Health Check URL')
output healthCheckUrl string = 'https://${containerApp.outputs.fqdn}/health'

@description('Management API URL')
output managementApiUrl string = 'https://${containerApp.outputs.fqdn}/api/v1'
