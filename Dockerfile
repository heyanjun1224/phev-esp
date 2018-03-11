FROM gcc:4.9
RUN \
  apt-get update && \
  apt-get install -y ruby
RUN gem install bundler
WORKDIR /usr/src
RUN \ 
    git clone https://github.com/DaveGamble/cJSON.git && \
    git clone --recursive https://github.com/throwtheswitch/cmock.git && \
    git clone --recursive https://github.com/espressif/esp-idf.git
WORKDIR /usr/src/cmock
RUN bundle install
COPY . /usr/src/phev-esp
WORKDIR /usr/src/phev-esp
ENV CMOCK_DIR /usr/src/cmock
ENV CJSON_DIR /usr/src/cJSON
RUN make setup
RUN make local