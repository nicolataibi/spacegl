#!/bin/bash
# SPACEGL - Local RPM Compliance Check
set -e

PROJECT_DIR=$(pwd)
BUILD_DIR="${PROJECT_DIR}/_local_build"
NAME="spacegl"
VERSION=$(grep "^Version:" spacegl.spec | awk '{print $2}')

echo "--- Preparing Local Build Tree ---"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Create source tarball
echo "--- Creating Source Tarball ---"
tar --exclude="./_local_build" --exclude="./.git" --transform "s|^\.|${NAME}-${VERSION}|" -czf "${BUILD_DIR}/SOURCES/${NAME}-${VERSION}.tar.gz" .

# Copy spec
cp spacegl.spec "${BUILD_DIR}/SPECS/"

echo "--- Running Local RPM Build (Binary only, skipping heavy compilation if possible) ---"
# Usiamo -bi (install section) per verificare dove finiscono i file senza compilare tutto se possibile, 
# ma rpmlint è meglio farlo sui pacchetti finiti (-bb).
# Se la compilazione è troppo lunga, possiamo fare un "mock" del build.
rpmbuild --define "_topdir ${BUILD_DIR}" -bb "${BUILD_DIR}/SPECS/spacegl.spec" --nodeps

echo ""
echo "--- RUNNING RPMLINT ON GENERATED PACKAGES ---"
rpmlint "${BUILD_DIR}/RPMS/x86_64/"*.rpm "${BUILD_DIR}/RPMS/noarch/"*.rpm
