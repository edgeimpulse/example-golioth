# Golioth Edge Impulse Demo

This application demonstrates use of the [Edge Impulse C++ Inferencing
Libarary](https://docs.edgeimpulse.com/docs/run-inference/cpp-library) with the
[Golioth Firmware SDK](https://github.com/golioth/golioth-firmware-sdk) on a
[Nordic
Thingy91](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-91)
with [Zephyr](https://docs.zephyrproject.org/latest/index.html) ([nRF Connect
SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK)).
It uses a model based on the [continuous gestures
dataset](https://docs.edgeimpulse.com/docs/pre-built-datasets/continuous-gestures).
Gesture classification results are streamed to Golioth's [timeseries
database](https://docs.golioth.io/application-services/lightdb-stream/), while
raw accelerometer sensor readings are delivered to an object storage bucket
where they can later be imported into Edge Impulse for model training. Data
routing is configured using [Golioth
Pipelines](https://docs.golioth.io/data-routing).

## Project Setup

You will need Golioth and Edge Impulse accounts to use this application. After
signing up, go through the following steps.

1. Setup your Zephyr workspace.

```
west init -m https://github.com/golioth/example-edge-impulse.git --mf west-ncs.yml
```

```
west update
```

2. Create a project on Golioth.
3. Create a project on Edge Impulse.

## Golioth Pipelines

In order to deliver classification results and raw accelerometer data to their
respective destinations, two pipelines must be created in the target Golioth
project.

**[Classification Results LightDB Stream](./pipelines/class-lightdb-stream.yml)**

This pipeline accepts CBOR data on the `/class` path and converts it to JSON
before delivering it to LightDB Stream, Golioth's timeseries database.

Click [here](https://console.golioth.io/pipeline?name=Classification%20Results%20LightDB%20Stream&pipeline=ZmlsdGVyOgogIHBhdGg6ICIvY2xhc3MiCiAgY29udGVudF90eXBlOiBhcHBsaWNhdGlvbi9jYm9yCnN0ZXBzOgogIC0gbmFtZTogY29udmVydC1zZW5kCiAgICB0cmFuc2Zvcm1lcjoKICAgICAgdHlwZTogY2Jvci10by1qc29uCiAgICAgIHZlcnNpb246IHYxCiAgICBkZXN0aW5hdGlvbjoKICAgICAgdHlwZTogbGlnaHRkYi1zdHJlYW0KICAgICAgdmVyc2lvbjogdjE=) to create this pipeline in your project.

**[Accelerometer S3](./pipelines/accel-s3.yml)**

This pipeline accepts the binary accelerometer readings and delivers them to an
Amazon S3 object storage bucket, where they can later be imported into Edge
Impulse.

> [!NOTE] Make sure to create
> [secrets](https://docs.golioth.io/data-routing/secrets/) for the referenced
> `AWS_ACCESS_KEY` and `AWS_SECRET_KEY`, and update the `name` and `region` to
> match your bucket.

Click [here](https://console.golioth.io/pipeline?name=Accelerometer%20S3&pipeline=ZmlsdGVyOgogIHBhdGg6ICIvYWNjZWwiCnN0ZXBzOgogIC0gbmFtZTogc2VuZC1zMwogICAgZGVzdGluYXRpb246CiAgICAgIHR5cGU6IGF3cy1zMwogICAgICB2ZXJzaW9uOiB2MQogICAgICBwYXJhbWV0ZXJzOgogICAgICAgIG5hbWU6IDxteS1idWNrZXQtbmFtZT4KICAgICAgICBhY2Nlc3Nfa2V5OiAkQVdTX0FDQ0VTU19LRVkKICAgICAgICBhY2Nlc3Nfc2VjcmV0OiAkQVdTX1NFQ1JFVF9LRVkKICAgICAgICByZWdpb246IDxteS1idWNrZXQtcmVnaW9uPg==) to create this pipeline in your project.

## Edge Impulse Inferencing Library

To perform gesture classification on-device, a model and inferencing library
will need to be generated. Follow the instructions in the [continuous motion
recognition
tutorial](https://docs.edgeimpulse.com/docs/tutorials/end-to-end-tutorials/continuous-motion-recognition)
to generate and download a C++ library.

After downloading, use the following commands to extract components and include
them in the application directory.

```
mkdir ei-generated
```

```
unzip -q <path-to-generated>.zip -d ei-generated/
```

```
mv ei-generated/edge-impulse-sdk/ .
mv ei-generated/model-parameters/ .
mv ei-generated/tflite-model/ .
```

## Build and Flash Firmware

After acquiring all necessary components, the firmware application can be built
using the following command.

```
west build -p -b thingy91_nrf9160_ns app/
```

After successful build, the device can be programmed with the following command.

```
west flash
```

The firmware requires
[credentials](https://docs.golioth.io/firmware/golioth-firmware-sdk/authentication/)
in order to communicate with Golioth. Credentials can be provided at runtime
using the [Zephyr
shell](https://docs.zephyrproject.org/latest/services/shell/index.html) via a
serial connection to the device.

```
uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
uart:~$ settings set golioth/psk <my-psk>
```

## Data Acquisition

On button press, the application will start sampling data from the Thingy91's
accelerometer, then use the trained model to classify the motion as a gesture
(e.g. wave, snake, etc.). The results of the classification, as well as the raw
sampled data will then be streamed to Golioth.

The following is an example of classification data that can be viewed in the
Golioth console.

```json
{
  "idle": 0.28515625,
  "snake": 0.16796875,
  "updown": 0.21875,
  "wave": 0.328125
}
```

Accelerometer data is uploaded as an array of floats representing X-Y-Z position
data. Each sampling will result in a new object being created in the specified
S3 bucket.
