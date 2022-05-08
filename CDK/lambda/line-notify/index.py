import boto3
import json
import os
import urllib.error
import urllib.request

def handler(event, context):
    print(json.dumps(event))

    # get the token from Parameter Store
    ssm = boto3.client('ssm')
    ssm_response = ssm.get_parameter(
        Name = os.environ['SSM_LINE_TOKEN'],
        WithDecryption = True
    )
    line_notify_token = ssm_response['Parameter']['Value']

    if 'eventType' in event:
        # connection event
        if event['eventType'] == 'connected':
            message = 'message=体重計の電源が入りました'
        else:
            message = 'message=体重計の電源が切れました'
    else:
        # weight notice event
        message = 'message=モチの体重は ' + str(event['weight']) + " g です。"

    # send message to LINE
    # https://notify-bot.line.me/doc/ja/
    request = urllib.request.Request(
        'https://notify-api.line.me/api/notify',
        headers={
            'Authorization': f'Bearer {line_notify_token}',
            'Content-Type': 'application/x-www-form-urlencoded'
        },
        data=message.encode()
    )

    with urllib.request.urlopen(request, timeout=1) as response:
        response_data = json.loads(response.read())
    print(response_data)
