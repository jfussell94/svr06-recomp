@echo off
title SvR06 build watcher
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0watch_build.ps1"
