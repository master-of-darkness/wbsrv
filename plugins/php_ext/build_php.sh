#!/bin/bash

# Define the PHP source directory
PHP_SRC_DIR="php-src"

if [ ! -d "$PHP_SRC_DIR" ]; then
    echo "Error: PHP source directory '$PHP_SRC_DIR' does not exist."
    exit 1
fi
cd "$PHP_SRC_DIR" || exit
./buildconf
echo "Configuring PHP build..."
./configure \
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
    --with-zlib-dir=/usr/include

# Compile PHP
echo "Building PHP..."
make -j$(nproc)