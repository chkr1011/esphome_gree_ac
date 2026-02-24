#!/bin/bash
set -e

# Check for python3
if ! command -v python3 &> /dev/null
then
    echo "python3 could not be found. Please install it."
    exit 1
fi

# Create virtual environment if it doesn't exist
if [ ! -d ".venv_sniffer" ]; then
    echo "Creating virtual environment in .venv_sniffer..."
    python3 -m venv .venv_sniffer
fi

# Activate virtual environment
source .venv_sniffer/bin/activate

# Install dependencies
echo "Ensuring dependencies are installed..."
pip install pyserial

# Run the script
echo "Starting the sniffer..."
# Use exec to replace the shell process with python
exec python3 analyze_dongle.py
