#!/bin/bash

# Define the PHP source directory
PHP_SRC_DIR="php-src"

if [ ! -d "$PHP_SRC_DIR" ]; then
    echo "Error: PHP source directory '$PHP_SRC_DIR' does not exist. Make sure you have updated git modules";
fi
cd "$PHP_SRC_DIR"
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
    --with-freetype \
    --enable-embed=shared \
    --with-mysqli \
    --with-pdo-mysql \
    --enable-opcache \
    --enable-sockets \
    --enable-gd \
    --with-xsl \

# Compile PHP
echo "Building PHP..."
make -j$(nproc)