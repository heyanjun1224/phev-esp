FROM gcc:latest
RUN \
  apt-get update && \
  apt-get install -y jq ccache ruby flex bison gperf python python-serial python-pip build-essential gcc clang git libssl-dev autoconf openssl libtool cmake doxygen pkg-config unzip wget
RUN pip install pyserial
WORKDIR /usr/esp
RUN wget -q https://dl.espressif.com/dl/xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz
RUN tar -xzf xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz
RUN gem install bundler
WORKDIR /usr/src
RUN git clone https://github.com/benmcollins/libjwt.git
RUN git clone https://github.com/DaveGamble/cJSON.git
RUN git clone https://github.com/papawattu/jansson.git
RUN cd jansson && cmake . && make && make install
RUN cd cJSON && mkdir build && cd build && cmake .. && make && make install
RUN cd libjwt && autoreconf -i && ./configure && make && make install
WORKDIR /usr/src
RUN \ 
    git clone --recursive https://github.com/throwtheswitch/cmock.git && \
    git clone --recursive https://github.com/espressif/esp-idf.git 
WORKDIR /usr/src/esp-idf/components
RUN \
    git clone --recursive https://github.com/papawattu/espmqtt.git && \
    git clone --recursive https://github.com/papawattu/libjwt.git && \
    git clone --recursive https://github.com/papawattu/jansson.git
RUN cd jansson && cmake .
WORKDIR /usr/src/cmock
RUN bundle install
#RUN export CLOUD_SDK_REPO="cloud-sdk-$(lsb_release -c -s)" && \
#    echo "deb http://packages.cloud.google.com/apt $CLOUD_SDK_REPO main" | tee -a /etc/apt/sources.list.d/google-cloud-sdk.list && \
#    curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | apt-key add - && \
#    apt-get update -y && apt-get install google-cloud-sdk -y
RUN curl https://sdk.cloud.google.com | bash
ENV PATH $PATH:/root/google-cloud-sdk/bin
ENV CMOCK_DIR /usr/src/cmock
ENV CJSON_DIR /usr/src/cJSON
#ENV LD_LIBRARY_PATH /usr/local/lib
#ENV LD_RUN_PATH /usr/local/lib
COPY . /usr/src/phev-esp
WORKDIR /usr/src/phev-esp
RUN make test
RUN make test
ENV IDF_PATH /usr/src/esp-idf
ENV PATH $PATH:/usr/esp/xtensa-esp32-elf/bin
ENV PATH /usr/esp/xtensa-esp32-elf/bin:$IDF_PATH/tools:$PATH
RUN date +%s > /root/build_number
RUN BUILD_NUMBER=`cat /root/build_number` make
RUN cp /usr/src/phev-esp/build/phev-esp.bin /root/firmware-`cat /root/build_number`.bin
ENV GOOGLE_PROJECT phev-db3fa
ENV GOOGLE_ACCOUNT configupdate@phev-db3fa.iam.gserviceaccount.com
RUN gcloud config set project ${GOOGLE_PROJECT}
RUN gcloud config set account ${GOOGLE_ACCOUNT}
RUN gcloud auth activate-service-account --key-file main/resources/phev-config-update.json
RUN gsutil cp /root/firmware-`cat /root/build_number`.bin gs://espimages/develop/ 
#RUN gcloud auth login
#RUN gcloud auth application-default login
RUN gcloud iot devices configs get-value --device my-device2 --region us-central1 --registry my-registry | jq .update.latestBuild=`cat /root/build_number` > /root/config.json
RUN gcloud iot devices configs update --config-file /root/config.json --device my-device2 --region us-central1 --registry my-registry
RUN cat /root/build_number