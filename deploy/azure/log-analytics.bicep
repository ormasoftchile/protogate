// deploy/azure/log-analytics.bicep
// Log Analytics Workspace for Container Apps monitoring

targetScope = 'resourceGroup'

@description('Location for the workspace')
param location string = resourceGroup().location

@description('Log Analytics Workspace name')
param logAnalyticsName string

@description('Resource tags')
param tags object = {}

@description('Retention in days')
@minValue(30)
@maxValue(730)
param retentionInDays int = 30

@description('Workspace SKU')
@allowed([
  'PerGB2018'
  'Free'
  'Standalone'
  'PerNode'
  'Standard'
  'Premium'
])
param sku string = 'PerGB2018'

// ========================================
// Log Analytics Workspace
// ========================================

resource logAnalyticsWorkspace 'Microsoft.OperationalInsights/workspaces@2022-10-01' = {
  name: logAnalyticsName
  location: location
  tags: tags
  properties: {
    sku: {
      name: sku
    }
    retentionInDays: retentionInDays
    features: {
      enableLogAccessUsingOnlyResourcePermissions: true
    }
    workspaceCapping: {
      dailyQuotaGb: 1 // 1GB daily cap for cost control
    }
    publicNetworkAccessForIngestion: 'Enabled'
    publicNetworkAccessForQuery: 'Enabled'
  }
}

// ========================================
// Outputs
// ========================================

@description('Log Analytics Workspace ID')
output workspaceId string = logAnalyticsWorkspace.id

@description('Log Analytics Workspace Customer ID')
output customerId string = logAnalyticsWorkspace.properties.customerId

@description('Log Analytics Workspace Primary Shared Key')
output workspaceKey string = logAnalyticsWorkspace.listKeys().primarySharedKey

@description('Log Analytics Workspace name')
output workspaceName string = logAnalyticsWorkspace.name
