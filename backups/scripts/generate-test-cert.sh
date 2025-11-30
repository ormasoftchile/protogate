#!/bin/bash
# Generate self-signed TLS certificate for testing

set -euo pipefail

DAYS=365
KEY_SIZE=2048

usage() {
    cat << EOF
Usage: $0 [options]

Generate self-signed TLS certificate for testing

Options:
    --domain <domain>   Domain name for certificate (default: localhost)
    --days <days>       Certificate validity in days (default: $DAYS)
    --output <dir>      Output directory (default: ./azure)

Examples:
    $0
    $0 --domain test.example.com --days 730
    $0 --domain "*.tunnel.example.com" --output ./certs
EOF
    exit 1
}

# Default values
DOMAIN="localhost"
OUTPUT_DIR="./azure"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --domain)
            DOMAIN="$2"
            shift 2
            ;;
        --days)
            DAYS="$2"
            shift 2
            ;;
        --output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Create output directory
mkdir -p "$OUTPUT_DIR"

CERT_FILE="$OUTPUT_DIR/tls-cert.pem"
KEY_FILE="$OUTPUT_DIR/tls-key.pem"

echo "Generating self-signed certificate..."
echo "  Domain: $DOMAIN"
echo "  Validity: $DAYS days"
echo "  Output: $OUTPUT_DIR"

# Generate private key and certificate
openssl req -x509 -newkey rsa:$KEY_SIZE -nodes \
    -keyout "$KEY_FILE" \
    -out "$CERT_FILE" \
    -days "$DAYS" \
    -subj "/CN=$DOMAIN" \
    -addext "subjectAltName=DNS:$DOMAIN" \
    2>/dev/null

if [ $? -eq 0 ]; then
    echo "✓ Certificate generated successfully"
    echo "  Certificate: $CERT_FILE"
    echo "  Private key: $KEY_FILE"
    
    # Display certificate info
    echo ""
    echo "Certificate details:"
    openssl x509 -in "$CERT_FILE" -noout -subject -dates
    
    exit 0
else
    echo "✗ Failed to generate certificate"
    exit 1
fi
