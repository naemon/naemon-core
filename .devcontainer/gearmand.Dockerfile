# Gearman job server used by the mod_gearman and statusengine broker modules.
#
# Built from the distribution package instead of a third party image so that
# this dev environment does not depend on anything outside Ubuntu.
FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends gearman-job-server \
    && rm -rf /var/lib/apt/lists/*

USER gearman
ENTRYPOINT ["/usr/sbin/gearmand"]
# --log-file=stderr keeps the log in "docker compose logs" instead of making
# gearmand complain about not being able to write /var/log/gearmand.log.
CMD ["--listen=0.0.0.0", "--port=4730", "--threads=10", "--job-retries=0", "--verbose=INFO", "--log-file=stderr"]
