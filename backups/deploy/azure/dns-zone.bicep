// deploy/azure/dns-zone.bicep
// DNS Zone for wildcard routing to Container App

targetScope = 'resourceGroup'

@description('Location for the DNS Zone (global)')
param location string = 'global'

@description('DNS domain name (e.g., example.com)')
param dnsDomainName string

@description('Resource tags')
param tags object = {}

// ========================================
// DNS Zone
// ========================================

resource dnsZone 'Microsoft.Network/dnsZones@2018-05-01' = {
  name: dnsDomainName
  location: location
  tags: tags
  properties: {
    zoneType: 'Public'
  }
}

// ========================================
// Outputs
// ========================================

@description('DNS Zone name')
output dnsZoneName string = dnsZone.name

@description('DNS Zone name servers')
output nameServers array = dnsZone.properties.nameServers

@description('DNS Zone resource ID')
output dnsZoneId string = dnsZone.id
