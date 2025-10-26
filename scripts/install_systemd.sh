#!/bin/bash

echo "Installing casproxyserver service..."
sudo cp casproxyserver.service /etc/systemd/system/casproxyserver.service

echo "Reloading systemd daemon..."
sudo systemctl daemon-reload

echo "Enabling and starting casproxyserver service..."
sudo systemctl enable --now casproxyserver.service

echo "Done. casproxyserver is now running."