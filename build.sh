#!/bin/bash

# Define the PHP source directory
PHP_SRC_DIR="deps/php-src"

# Check if the directory exists
if [ ! -d "$PHP_SRC_DIR" ]; then
    echo "Error: PHP source directory '$PHP_SRC_DIR' does not exist."
    exit 1
fi

# Navigate to the PHP source directory
cd "$PHP_SRC_DIR" || exit

# Clean any previous builds
echo "Cleaning previous builds..."
make clean || true
./buildconf

# Configure the build with the specified options
echo "Configuring PHP build..."
./configure \
    --enable-debug \
    --enable-cli \
    --enable-zts \
    --with-zlib \
    --with-curl \
    --with-openssl \
    --enable-mbstring \
    --with-libdir=/usr/lib/x86_64-linux-gnu \
    --with-jpeg \
    --with-png \
    --with-freetype \
    --enable-so \
    --enable-embed=shared \
    --with-mysqli \
    --with-pdo-mysql \
    --enable-opcache \
    --enable-sockets \
    --enable-gd \
    --with-gd \
    --with-xsl \
    --with-xmlrpc \
    --with-zlib-dir=/usr/include

# Compile PHP
echo "Building PHP..."
make -j$(nproc)

# Notify the user of successful build
if [ $? -eq 0 ]; then
    echo "PHP with MySQL support has been successfully built."
else
    echo "PHP build failed."
    exit 1
fi
