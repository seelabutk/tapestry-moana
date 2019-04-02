# Setting up AWS for Tapestry/Moana Demo

Documentation for setting up the Tapestry/Moana demo on AWS.

## Preparing the data

The Moana Island data needs to be placed into an EBS volume and then backed up
to an S3 snapshot.  The snapshot is what the cluster will use to access the
data.

Intel AWS organization snapshot containing Moana Island data with BIFF files:
snap-0826763b2230ee239.

There are three steps to setting up the data (and setting up other necessary
items), described as follows.

## Creating an initial instance, security group, and SSH key pair

It's useful to go through these steps whether the snapshot is available or not
so that the demo security group and SSH key pair can be created. It is also
helpful to know how to interface with individual instances when troubleshooting
clusters.

1.  Create an EC2 instance that is "good enough" for basic commands like `wget`
    and `tar`. From the EC2 console:
    1.  Hit Launch Instance
    2.  Select Amazon Linux 2 AMI (x86)
    3.  Choose an instance size. I chose t2.large
    4.  Hit Configure Instance Details
    5.  Hit Add Storage
    6.  Hit Add New Volume to create an EBS volume
        1.  Volume type should be EBS
        2.  Leave device as `/dev/sdb`
        3.  Leave snapshot blank
        4.  Set size to an appropriate value. I chose 200 GB
        5.  Make sure Delete on Termination is UNCHECKED
        6.  On the Intel AWS org, the volume should be encrypted
    7.  Hit Add Tags
    8.  Hit Configure Security Group. We will be creating the security group
        our cluster will use at this point just to get it out of the way
        1.  For inbound rules, create an HTTP rule for 0.0.0.0,::/0, HTTPS for
            0.0.0.0,::/0, and SSH for 0.0.0.0,::/0. This will allow any IP
            address to connect via ports 80 and 443 with TCP connections and
            port 22 for SSH connections (however you will need a key to login).
            Create a custom rule for port 8861 for the demo server instances to
            use
        2.  Create the same rules for outbound connections
    9.  Hit Launch
    10. You will need to create an SSH key
        1.  Give the key a name and hit Download Key Pair
        2.  This key can be reused for every instance we launch in the future,
            including those in our cluster, so we can add it to our SSH
            configuration to make logging in easy. Save the key somewhere
            known, e.g. to `~/.ssh/`
        3.  Edit your local SSH config, `~/.ssh/config` and add an entry:
            
            ```
            Host *.compute.amazonaws.com
                User ec2-user
                IdentityFile ~/.ssh/<your_file>.pem
            ```

2.  The management console will show the instance start up and give you a DNS
    hostname and IP address. From the terminal, run `ssh <hostname>` and the
    config file above should handle using your key to log you in
3.  Mount the EBS volume to your instance
    1.  Run `lsblk` to find the device name of your 200 GB EBS volume. Usually
        it is `/dev/nvme1n1` since it is the only EBS volume attached
    2.  Run `mount /dev/<ebs_device> /mnt` to mount your volume
4.  Launch a terminal multiplexer of your choice (e.g. `tmux` or `screen`) or
    `nohup` to download the data from
    https://www.technology.disneyanimation.com/islandscene
5.  Get some coffee while it downloads
6.  Extract the data with `tar xf island-basepackage-v1.1.tgz`

### Preparing the BIFFs

You will need to create BIFFs from the raw data to use with OSPRay.

### Creating an S3 snapshot

Creating a snapshot from the volume will allow the data to be copied into new
EBS volumes dynamically when instances are launched into the demo cluster.

1.  From the management console, go to Volumes under Elastic Block Store
2.  Select your EBS volume with the data
3.  At this point it's a good idea to add a name to the volume, e.g.
    `moana-island-data` so that you can more easily reference it. Hover over
    the blank Name field and click the edit icon to change the name
4.  Go to Actions -> Create Snapshot
5.  Give the snapshot a description. You can also add a tag with key "Name" and
    value whatever you would like to call the snapshot (again for easier
    reference in the future)

## Using AWS' built-in auto scaling

This method removes some/most of the burden of sysadmin on the user. The gist
is that AWS will scale up a cluster automatically, allocating new nodes and/or
tasks for you.

1.  Upload data to an EBS volume
    1.  Create an EC2 instance that is good enough for running basic commands,
        e.g. `wget` and `tar`
    2.  When creating the EC2 instance, create an EBS volume with enough storage
        space, e.g. 200 GB. MAKE SURE that `Delete on termination` is unchecked!
        Otherwise the volume will be erased when the EC2 instance stops
    3.  Make sure the EC2 instance has the right security group settings. It
        will need inbound rules that are SSH type (port 22) and open to all
        traffic (0.0.0.0/0 and ::/0). It will also need outbound rules that are
        HTTP type (port 80) and open to all traffic
    4.  You will probably need to make the filesystem for the volume. Inside
        the EC2 instance, run `lsblk` to see what the name of the volume is.
        Then run `mkfs.ext4 /dev/<name>` to create a mountable filesystem.
        Finally run `mount /dev/<name> /mnt/` to access it under `/mnt`
    5.  Now you can download or copy the data; just make sure it lands within
        the mounted filesystem so that it is not erased when the instance is
        stopped
2.  Upload your Docker image to an ECR repository
    1.  Create an ECR repository. Make sure the name you give is the name of
        the docker image you will be pushing! For example, if you created/will
        create the `tapestry-moana-demo:latest` image, the ECR repository
        should be called `tapestry-moana-demo`
    2.  To push to the repository, you will need to create an IAM user
        1.  Create a new IAM user from the IAM Management Console. Make sure
            their access at least contains programmatic access via the CLI
        2.  Choose to create a new group for the user, and add the
            administrator access policy
        3.  TAKE NOTE of the access key ID and secret access key, as you won't
            be able to see these again
    3.  To push to the repository, you will need to get the AWS CLI. Use `pip`
        to install the latest version via `pip install awscli`
    4.  Configure the AWS CLI with `aws configure` to link your IAM user to the
        tool. Set the access key ID and secret access key to the values you got
        when creating the user. Set the default region to the region your EC2
        instance was/will be in (`us-east-2` for me). Set the default output
        format to `json`
    5.  Build your docker image
    6.  Login to Docker using the login information provided by AWS CLI. Run
        `aws ecr get-login --no-include-email --region <your region>` to get
        the exact command. It may be that you need to modify this command if it
        fails in the next step
        *   Note that you actually have to run the resulting command!
    7.  Use the ECR-provided instructions for pushing to your ECR repository,
        e.g. `docker tag <image name>:latest <repository URI>:latest` to tag
        your image, and `docker push <repository URI>:latest` to push it. If
        you get an error when pushing saying `no basic auth credentials`, then
        re-run this modified login command: `aws ecr get-login
        --no-include-email --region <your region> | sed 's|https://||'`
        *   Some context on this. The docker login command will store the
            server and key as a key-value pair. However, when it stores the
            server as `https://<server ID>.amazonaws.com`, it will fail during
            a command that looks up credentials, because the command will be
            looking for `<server ID>.amazonaws.com` (without the `https`). Thus
            we are initially storing the server without `https` to circumvent
            this
3.  Create an ECS Cluster
    1.  Use an instance type that is large enough for the demo. For example, I
        chose m5.24xlarge
    2.  Launch with 1 instance to test with
    3.  Use the default VPC, and the region-default subnet to allow public
        access. Use the security group you made before
4.  Check that the new machines joined your ECS Cluster "Default" under "EC2
    instances"
5.  Create a "Service" under that cluster, with the repository you created
    (there's one other step. Along the left is "Cluster", "???", and
    "Repository". You've gotta create a "???")
6.  Remember that there needs to be a load balancer and go set that up. Probably
    a "tcp-level load balancer" which mimics Docker's
    ingress/swarm/overlay/whatever network. You should also make a "Target
    Group" of type "Instance" (I think)
7.  For your cluster service, set it to use that load balancer and that target
    group. For your sanity, don't let it create a target group for you
8.  Remember that you need to disable the health check (or minimize the damage
    it can do). Go to your target group, "Edit Health Check", and set the "time
    between checks" to a high number, say 1000. Edit the deregistration period
    small, 15s. Maybe something else there
9.  Go to your load balancer, find the URL, and go to that URL
10. Remember that you need your security group. For Tapestry on port 9010, I
    made a group called "allow-9010", and allowed connections from anywhere on
    port 9010. Remember to add that security group to every single other thing
    you've made so far (launch config, load balancer, target group, service)
11. Go to that URL and see beauty (pff, sure. Actually, repeat the above steps
    10 times until you get it right).

## Manual instance creation

This method requires creating an AMI that you launch multiple times manually,
and requires more sysadmin work

1. Create a launch configuration for a normal Ubuntu instance with port 22 open
2. Launch a bunch of instances
3. Log in to each one, setting up Docker and creating a swarm like you would
   normally
4. Now you have a Docker swarm and can do whatever you want
