import { Stack, StackProps, Duration } from 'aws-cdk-lib'
import { Construct } from 'constructs'
import { Role, ServicePrincipal, Policy, PolicyStatement, Effect, ManagedPolicy } from 'aws-cdk-lib/aws-iam'
import {
  Dashboard,
  TextWidget,
  GraphWidget,
  Metric,
  Statistic,
  Row,
  Column,
  SingleValueWidget
} from 'aws-cdk-lib/aws-cloudwatch'
import { CfnTopicRule } from 'aws-cdk-lib/aws-iot'
import { LogGroup, RetentionDays } from 'aws-cdk-lib/aws-logs'
import { Function, Runtime, Code } from 'aws-cdk-lib/aws-lambda'
import { StringParameter } from 'aws-cdk-lib/aws-ssm'

const path = require('path')

const SSM_LINE_TOKEN = '/CatSensor/LineNotifyToken'

export class CatSensorStack extends Stack {
  constructor(scope: Construct, id: string, props?: StackProps) {
    super(scope, id, props)

    const region = Stack.of(this).region
    const accountId = Stack.of(this).account

    const lineNotifyToken = StringParameter.fromSecureStringParameterAttributes(this, 'LineNotifyTokenParameter', {
      parameterName: SSM_LINE_TOKEN
    })

    // CloudWatch Metrics Action to CloudWatch Logs Role
    const ioTRuleCWMetricsRole = new Role(this, 'IoTRuleCWMetricsRole', {
      assumedBy: new ServicePrincipal('iot.amazonaws.com')
    })
    const ioTRuleCWMetricsPolicy = new Policy(this, 'IoTRuleCWMetricsPolicy', {
      statements: [
        new PolicyStatement({
          actions: ['cloudwatch:PutMetricData'],
          resources: ['*'],
          effect: Effect.ALLOW
        })
      ]
    })
    ioTRuleCWMetricsRole.attachInlinePolicy(ioTRuleCWMetricsPolicy)

    // Action to CloudWatch Logs Role
    const ioTStatusRuleActionToLogsRole = new Role(this, 'IoTStatusRuleActionToLogsRole', {
      assumedBy: new ServicePrincipal('iot.amazonaws.com')
    })

    // Action destination Log Group
    const ioTStatusRuleActionLogGroup = new LogGroup(this, 'IoTStatusRuleActionLogGroup', {
      retention: RetentionDays.ONE_MONTH
    })
    ioTStatusRuleActionLogGroup.grantWrite(ioTStatusRuleActionToLogsRole)

    // Error Action to CloudWatch Logs Role
    const ioTRuleErrorActionToLogsRole = new Role(this, 'IoTRuleErrorActionToLogsRole', {
      assumedBy: new ServicePrincipal('iot.amazonaws.com')
    })
    const ioTRuleErrorActionToLogsPolicy = new ManagedPolicy(this, 'IoTRuleErrorActionToLogsPolicy', {
      statements: [
        new PolicyStatement({
          actions: ['logs:DescribeLogStreams'],
          effect: Effect.ALLOW,
          resources: ['*']
        })
      ]
    })
    ioTRuleErrorActionToLogsPolicy.attachToRole(ioTRuleErrorActionToLogsRole)

    // Error Action destination Log Group
    const ioTRuleErrorActionLogGroup = new LogGroup(this, 'IoTRuleErrorActionLogGroup', {
      retention: RetentionDays.ONE_MONTH
    })
    ioTRuleErrorActionLogGroup.grantWrite(ioTRuleErrorActionToLogsRole)

    const lineNotifyFunction = new Function(this, 'LineNotifyFunction', {
      runtime: Runtime.PYTHON_3_9,
      handler: 'index.handler',
      code: Code.fromAsset(path.join(__dirname, '../lambda/line-notify')),
      logRetention: RetentionDays.THREE_MONTHS,
      environment: {
        SSM_LINE_TOKEN: SSM_LINE_TOKEN
      }
    })
    lineNotifyFunction.addPermission('InvokeByIoTRulePermission', {
      principal: new ServicePrincipal('iot.amazonaws.com'),
      action: 'lambda:InvokeFunction'
    })
    lineNotifyToken.grantRead(lineNotifyFunction)

    // IoT Core Rule
    // Topic `catsensor/weight_data/#`
    new CfnTopicRule(this, 'CatSensorRule', {
      topicRulePayload: {
        sql: "SELECT *, topic(3) as device FROM 'catsensor/weight_data/#'",
        actions: [
          {
            cloudwatchMetric: {
              metricName: '${topic(3)}/weight',
              metricNamespace: 'CatSensor',
              metricUnit: 'None',
              metricValue: '${weight}',
              roleArn: ioTRuleCWMetricsRole.roleArn
            }
          },
          {
            lambda: {
              functionArn: lineNotifyFunction.functionArn
            }
          }
        ],
        errorAction: {
          cloudwatchLogs: {
            logGroupName: ioTRuleErrorActionLogGroup.logGroupName,
            roleArn: ioTRuleErrorActionToLogsRole.roleArn
          }
        }
      }
    })

    // AWS IoT Core LifeCycle Event to catch connect/disconnect
    new CfnTopicRule(this, 'CatSensorConnectionRule', {
      topicRulePayload: {
        ruleDisabled: true,
        sql: "SELECT * FROM '$aws/events/presence/+/WeightMonitor'",
        actions: [
          {
            lambda: {
              functionArn: lineNotifyFunction.functionArn
            }
          }
        ],
        errorAction: {
          cloudwatchLogs: {
            logGroupName: ioTRuleErrorActionLogGroup.logGroupName,
            roleArn: ioTRuleErrorActionToLogsRole.roleArn
          }
        }
      }
    })

    // device status rule
    new CfnTopicRule(this, 'CatSensorStatusRule', {
      topicRulePayload: {
        sql: "SELECT *, topic(3) as device FROM 'catsensor/status/#'",
        actions: [
          {
            cloudwatchLogs: {
              logGroupName: ioTStatusRuleActionLogGroup.logGroupName,
              roleArn: ioTStatusRuleActionToLogsRole.roleArn
            }
          }
        ],
        errorAction: {
          cloudwatchLogs: {
            logGroupName: ioTRuleErrorActionLogGroup.logGroupName,
            roleArn: ioTRuleErrorActionToLogsRole.roleArn
          }
        }
      }
    })

    ////////////////////////////////////////////
    // 体重の dashboard

    //Create CloudWatch Dashboard
    const MochiDashboard = new Dashboard(this, 'MochiDashboard', {
      dashboardName: 'MochiWeight'
    })

    // タイトル
    MochiDashboard.addWidgets(
      new TextWidget({
        markdown: '# モチの体重',
        height: 2,
        width: 20
      })
    )

    MochiDashboard.addWidgets(
      new Row(
        new Column(
          new SingleValueWidget({
            metrics: [
              new Metric({
                metricName: 'WeightMonitor/weight',
                namespace: 'CatSensor',
                statistic: Statistic.AVERAGE,
                label: 'weight',
                period: Duration.days(1)
              })
            ],
            height: 4,
            width: 5
          }),
          new GraphWidget({
            leftYAxis: {
              showUnits: false,
              min: 3500,
              max: 5000
            },
            left: [
              new Metric({
                metricName: 'WeightMonitor/weight',
                namespace: 'CatSensor',
                statistic: Statistic.AVERAGE,
                label: 'weight',
                period: Duration.minutes(1)
              })
            ],
            width: 15
          })
        )
      )
    )
  }
}
