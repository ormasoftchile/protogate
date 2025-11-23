// deploy/azure/dns-record.bicep
// Creates wildcard A record pointing to Container App

targetScope = 'resourceGroup'

@description('DNS Zone name')
param dnsZoneName string

@description('Container App FQDN to extract IP from')
param containerAppFqdn string

@description('TTL for the DNS record')
param ttl int = 3600

// ========================================
// Wildcard A Record
// ========================================

// Note: This is a placeholder for wildcard routing
// In production, you would typically:
// 1. Use a CNAME record pointing to the Container App FQDN, OR
// 2. Use Azure Front Door or Application Gateway with custom domain
// 3. Configure custom domain directly in Container Apps

// For now, we create a CNAME record for the wildcard subdomain
resource wildcardRecord 'Microsoft.Network/dnsZones/CNAME@2018-05-01' = {
  name: '${dnsZoneName}/*'
  properties: {
    TTL: ttl
    CNAMERecord: {
      cname: containerAppFqdn
    }
  }
}

// ========================================
// Outputs
// ========================================

@description('Wildcard record FQDN')
output wildcardFqdn string = wildcardRecord.properties.fqdn
