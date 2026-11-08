@echo off
title SvR06 USA build
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0usa_build.ps1" %*
