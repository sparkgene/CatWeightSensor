#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib'
import { CatSensorStack } from '../lib/cat-sensor-stack'

const app = new cdk.App()
new CatSensorStack(app, 'CatSensorStack')
