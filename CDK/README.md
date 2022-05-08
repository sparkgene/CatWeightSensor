# デプロイ手順

## 前提条件

- Unix コマンドが実行可能な環境 (Mac, Linux, ...) を想定しています。もし手元にそのような環境がない場合は、AWS Cloud9 を利用することも可能です。
- `cdk` コマンドがインストールされている必要があります。`npm install -g aws-cdk` でインストールが可能です。
    - https://docs.aws.amazon.com/cdk/v2/guide/getting_started.html

### StringParameterの作成

Line Notify TokenをParameter Storeで管理するので、事前に登録しておきます。

```
aws ssm put-parameter \
  --name "/CatSensor/LineNotifyToken" \
  --type "SecureString" \
  --value "your token"
```
## リージョンを指定

```
export AWS_DEFAULT_REGION=us-east-1
```

## CDK によるデプロイメント

cdk コマンド初回利用時のみ、以下のコマンドを実行してください。すでに実行済みであれば不必要です。

```
cdk bootstrap
```


```
cdk synth
```

デプロイを実施します。以下のコマンドを実行してください。途中でデプロイ内容について確認されますが、`y`とタイプして `Enter` して先に進めてください。

```
cdk deploy
```

## リソースの削除

環境が不要になった場合は、以下のコマンドを実行してください。

```
cdk destroy
```