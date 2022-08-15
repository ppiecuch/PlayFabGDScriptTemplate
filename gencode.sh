#!/bin/bash

set -e

rm -rf PlayFabSDK_gen && mkdir -p PlayFabSDK_gen
docker run --rm -v $PWD/PlayFabSDK_gen:/app/PlayFabSDK playfab_godot
